#include "config_store.h"
#include "input_profile.h"
#include <stdio.h>

typedef struct config_seat {
    qa_input_seat *input,*publication_input;
    qa_cvars *cvars,*mouse;
    qa_seat_settings settings;
    qa_cvar_archive client_archive,mouse_archive;
    bool found,cvars_transferred;
} config_seat;
struct frontend_config_source {
    frontend_config_source *next;
    frontend_config_store *manager;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_launch_instance_lease *metadata;
    qa_console *console;
    qa_cvars *cvars,*movement,*fallback;
    frontend_config_files *files;
    frontend_key_profile *keys;
    frontend_startup_config *phase;
    qa_input_console *bindings;
    qa_command_context command;
    qa_cvar_archive source_archive,movement_archive,fallback_archive;
    config_seat seats[QA_INPUT_LOCAL_SEATS];
    size_t seat_count,seat_index;
    qa_console_dialect movement_dialect;
    bool primary,published,configured,released,running,write_registered,dump_registered,has_mod;
};
struct frontend_config_store {
    qa_frontend *frontend;
    qa_application_startup_hooks hooks;
    frontend_config_source *sources;
    frontend_config_source *prepared_primary;
    const qa_launch_snapshot *prepared;
    frontend_keys_publication key_publication;
    bool running;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool equal(const char *left,const char *right)
{
    for (;;++left,++right) {
        unsigned a=(unsigned char)*left,b=(unsigned char)*right;
        if (a>='A' && a<='Z') a+='a'-'A'; if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false; if (!a) return true;
    }
}
static const qa_launch_instance *instance(const frontend_config_source *source)
{ return qa_launch_instance_lease_view(source->metadata); }
static qa_settings_store input_store(frontend_config_source *source)
{
    qa_vfs *files=source->manager->frontend->input_config;
    if (files) for (size_t i=0;i<qa_vfs_mount_count(files);++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(files,i,&mount) && mount.writable) return (qa_settings_store){files,mount.id};
    }
    return frontend_config_files_store(source->files,false);
}
static void print(void *context,const char *text)
{
    frontend_config_source *source=context;
    frontend_console_print(source->manager->frontend,&source->command,text);
}
static qa_cvars *registry(frontend_config_source *source,qa_console_dialect dialect,qa_error *error)
{
    qa_cvar_options options={.dialect=dialect,.user=source,.print=print};
    return qa_cvars_create(&options,error);
}
static bool source_context(const frontend_config_source *source,const qa_command_context *command)
{
    return source && command && command->origin!=QA_COMMAND_REMOTE &&
        command->owner==source->command.owner && command->session==source->command.session &&
        command->dialect==source->command.dialect &&
        qa_application_command_context_active(source->application,command);
}
static bool current_command(const frontend_config_source *source,qa_command_context *command,qa_error *error)
{
    *command=source->command;
    command->registry=0; command->generation=0; command->actor=(qa_actor_id){0};
    return qa_application_capture_command_context(source->application,command,command,error) &&
        source_context(source,command);
}
frontend_config_source *frontend_config_store_source(const frontend_config_store *owner,const qa_console *console)
{
    if (owner && console) for (frontend_config_source *source=owner->sources;source;source=source->next)
        if (source->console==console) return source;
    return NULL;
}
frontend_config_files *frontend_config_source_files(const frontend_config_source *source) { return source?source->files:NULL; }
frontend_key_profile *frontend_config_source_keys(const frontend_config_source *source) { return source?source->keys:NULL; }
qa_cvars *frontend_config_source_cvars(const frontend_config_source *source) { return source?source->cvars:NULL; }
bool frontend_config_source_primary(const frontend_config_source *source) { return source && source->primary; }
bool frontend_config_source_published(const frontend_config_source *source) { return source && source->published; }
qa_input_seat *frontend_config_source_input(const frontend_config_source *source,uint32_t seat)
{ return source && seat<source->seat_count?source->seats[seat].input:NULL; }
qa_cvars *frontend_config_source_seat_cvars(const frontend_config_source *source,uint32_t seat)
{ return source && seat<source->seat_count?source->seats[seat].cvars:NULL; }
qa_cvars *frontend_config_source_mouse_cvars(const frontend_config_source *source,uint32_t seat)
{ return source && seat<source->seat_count?source->seats[seat].mouse:NULL; }
bool frontend_config_source_take_seat_cvars(frontend_config_source *source,uint32_t seat,qa_cvars **out,qa_error *error)
{
    if (!source || !out || *out || seat>=source->seat_count || !source->seats[seat].cvars ||
        source->seats[seat].cvars_transferred || source->running)
        return fail(error,QA_ERROR_ARGUMENT,"Client factory needs its unclaimed actual prepared registry");
    source->seats[seat].cvars_transferred=true; *out=source->seats[seat].cvars; return true;
}
static qa_input_seat *binding_seat(void *context,const qa_command_context *command)
{
    frontend_config_source *source=context;
    if (!source_context(source,command)) return NULL;
    uint32_t seat=command->origin==QA_COMMAND_SEAT?command->seat:0;
    if (!source->primary || seat>=source->seat_count) return NULL;
    if (source->published) {
        qa_frontend *f=source->manager->frontend;
        return seat<f->options.seats && f->seats?f->seats[seat].input:NULL;
    }
    return source->seats[seat].input;
}
qa_cvars *frontend_config_store_cvar_owner(const frontend_config_store *manager,const qa_console *console,
    const qa_command_context *command,const char *name)
{
    frontend_config_source *source=frontend_config_store_source(manager,console);
    if (!source_context(source,command) || !name) return NULL;
    if (qa_cvars_find(source->cvars,name)) return source->cvars;
    if (command->origin==QA_COMMAND_SEAT && command->seat>=source->seat_count) return NULL;
    config_seat *seat=command->origin==QA_COMMAND_SERVER || !source->seat_count?NULL:
        source->seats+(command->origin==QA_COMMAND_SEAT?command->seat:0);
    if (seat && qa_cvars_find(seat->mouse,name)) return seat->mouse;
    if (command->origin==QA_COMMAND_SEAT && seat && qa_cvars_find(seat->cvars,name)) return seat->cvars;
    if (source->movement && qa_cvars_find(source->movement,name)) return source->movement;
    if (source->fallback && qa_cvars_find(source->fallback,name)) return source->fallback;
    return command->origin==QA_COMMAND_SERVER?source->cvars:
        command->origin==QA_COMMAND_SEAT && seat?seat->cvars:source->fallback;
}
qa_cvars *frontend_config_store_visible_cvars(const frontend_config_store *manager,const qa_console *console,
    const qa_command_context *command,size_t ordinal)
{
    frontend_config_source *source=frontend_config_store_source(manager,console);
    if (!source_context(source,command)) return NULL;
    qa_cvars *rows[5]={0}; size_t count=0;
    uint32_t seat=command->origin==QA_COMMAND_SEAT?command->seat:0;
    if (command->origin!=QA_COMMAND_SERVER && seat<source->seat_count) rows[count++]=source->seats[seat].mouse;
    rows[count++]=source->cvars;
    if (command->origin==QA_COMMAND_SEAT && seat<source->seat_count) rows[count++]=source->seats[seat].cvars;
    if (source->movement) rows[count++]=source->movement;
    if (source->fallback && source->fallback!=source->movement) rows[count++]=source->fallback;
    return ordinal<count?rows[ordinal]:NULL;
}
static bool read(void *context,frontend_script_scope scope,const char *name,const qa_command_context *command,
    qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_source *source=context;
    return source_context(source,command) && frontend_config_files_read(source->files,scope,name,command,bytes,lease,error);
}
static void release(void *context,void *lease)
{ frontend_config_source *source=context; frontend_config_files_release(source->files,lease); }
static bool defaults(void *context,qa_error *error)
{
    frontend_config_source *source=context;
    if (!source->seat_count) return true;
    return qa_input_default_bindings(source->seats[source->seat_index].input,0,error);
}
static bool archive(void *context,qa_error *error)
{
    frontend_config_source *source=context;
    if (!source->seat_index &&
        (!qa_cvar_archive_apply(source->cvars,&source->source_archive,error) ||
         !qa_cvar_archive_apply(source->movement,&source->movement_archive,error) ||
         !qa_cvar_archive_apply(source->fallback,&source->fallback_archive,error))) return false;
    if (!source->seat_count) return true;
    config_seat *seat=source->seats+source->seat_index;
    if (!qa_cvar_archive_apply(seat->cvars,&seat->client_archive,error) ||
        !qa_cvar_archive_apply(seat->mouse,&seat->mouse_archive,error)) return false;
    if (!seat->found) return true;
    if (!qa_input_seat_replace_bindings(seat->input,seat->settings.bindings,seat->settings.binding_count,error) ||
        !qa_input_mouse_settings_write(seat->mouse,&seat->settings.mouse,error)) return false;
    *qa_input_seat_gamepad_tuning(seat->input)=seat->settings.gamepad;
    return !seat->settings.has_always_run || qa_cvars_set_flags(seat->mouse,"cl_run",
        seat->settings.always_run?"1":"0",QA_CVAR_ARCHIVE,error);
}
static bool launch(void *context,qa_error *error)
{
    frontend_config_source *source=context;
    (void)error;
    source->configured=true;
    return true;
}
static bool replay(void *context,qa_error *error)
{
    frontend_config_source *source=context;
    return source->seat_index || qa_application_startup_replay_variables(source->application,source->console,error);
}
static bool phase_create(frontend_config_source *source,qa_error *error)
{
    qa_command_context command=source->command;
    if (source->seat_count) { command.origin=QA_COMMAND_SEAT; command.seat=(uint32_t)source->seat_index; }
    if (!qa_application_capture_command_context(source->application,&command,&command,error)) return false;
    frontend_startup_config_options options={.command=command,
        .has_mod=source->has_mod,
        .seat_scope=source->seat_index!=0,.context=source,.read=read,.release=release,
        .apply_defaults=defaults,.apply_archive=archive,.apply_launch=launch,.replay_startup_variables=replay};
    source->phase=frontend_startup_config_create(&options,error);
    return source->phase!=NULL;
}
typedef struct config_filter {
    frontend_config_source *source;
    const qa_command_context *command;
} config_filter;
static bool routed_archive(void *context,const qa_cvars *registry,const qa_cvar_view *variable)
{
    const config_filter *filter=context;
    return frontend_config_store_cvar_owner(filter->source->manager,filter->source->console,
        filter->command,variable->name)==registry;
}
static bool config_text(frontend_config_source *source,const qa_command_context *command,
    const qa_input_seat *input,qa_buffer *out,qa_error *error)
{
    if (input && !qa_input_bindings_config(input,true,out,error)) return false;
    config_filter filter={source,command};
    for (size_t i=0;;++i) {
        qa_cvars *registry=frontend_config_store_visible_cvars(source->manager,source->console,command,i);
        if (!registry) break;
        qa_buffer rows={0};
        if (!qa_cvars_config_filtered(registry,routed_archive,&filter,&rows,error)) return false;
        if (rows.size>SIZE_MAX-out->size-1) { qa_buffer_free(&rows); return fail(error,QA_ERROR_MEMORY,"Source configuration exceeds text storage"); }
        uint8_t *text=realloc(out->data,out->size+rows.size+1);
        if (!text) { qa_buffer_free(&rows); return fail(error,QA_ERROR_MEMORY,"Retaining actual routed source configuration"); }
        out->data=text; if (rows.size) memcpy(text+out->size,rows.data,rows.size);
        out->size+=rows.size; text[out->size]=0; qa_buffer_free(&rows);
    }
    return true;
}
static bool config_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_config_source *source=context;
    if (!source_context(source,&command->context)) return fail(error,QA_ERROR_ARGUMENT,"Configuration command lost its actual source owner");
    const char *name=command->argv[0];
    if (equal(name,"condump")) {
        if (command->argc!=2) { print(source,"condump <filename>\n"); return true; }
        qa_frontend *f=source->manager->frontend;
        uint32_t seat=command->context.origin==QA_COMMAND_SEAT?command->context.seat:0;
        if (!f->seats || seat>=f->options.seats || !f->seats[seat].console)
            return fail(error,QA_ERROR_UNSUPPORTED,"Console dump requires its actual local console buffer");
        return frontend_config_files_dump(source->files,command->argv[1],&command->context,
            qa_seat_console_buffer(f->seats[seat].console),error);
    }
    if (command->argc>2) { print(source,"writeconfig [filename]\n"); return true; }
    const char *requested=command->argc==2?command->argv[1]:command->context.dialect==QA_CONSOLE_Q3?"q3config.cfg":"config.cfg";
    size_t length=strlen(requested);
    bool suffix=length>=4 && !strcmp(requested+length-4,".cfg");
    if (length>SIZE_MAX-5) return fail(error,QA_ERROR_MEMORY,"Configuration filename exceeds storage");
    char *path=malloc(length+(suffix?1:5));
    if (!path) return fail(error,QA_ERROR_MEMORY,"Retaining configuration output filename");
    memcpy(path,requested,length); memcpy(path+length,suffix?"":".cfg",suffix?1:5);
    qa_input_seat *input=binding_seat(source,&command->context);
    qa_buffer text={0};
    bool ok=config_text(source,&command->context,input,&text,error) &&
        frontend_config_files_write_config_text(source->files,path,&command->context,(qa_bytes){text.data,text.size},error);
    qa_buffer_free(&text);
    free(path); return ok;
}
static bool source_destroy(frontend_config_source *source,qa_error *error)
{
    if (source->running || (source->files && !frontend_config_files_idle(source->files)) ||
        !frontend_startup_config_destroy(source->phase,error)) return false;
    source->phase=NULL;
    qa_input_console_destroy(source->bindings); source->bindings=NULL;
    if (source->write_registered) qa_console_unregister(source->console,"writeconfig",source->command.owner);
    if (source->dump_registered) qa_console_unregister(source->console,"condump",source->command.owner);
    source->write_registered=source->dump_registered=false;
    if (source->keys) {
        if (!frontend_key_profile_detach(source->keys,source->cvars,error) ||
            !frontend_key_profile_release(source->keys,error)) return false;
    } else if (!frontend_config_files_destroy(source->files,error)) return false;
    for (size_t i=0;i<source->seat_count;++i) {
        config_seat *seat=source->seats+i;
        qa_input_seat_destroy(seat->input);
        if (!seat->cvars_transferred) qa_cvars_destroy(seat->cvars);
        qa_cvars_destroy(seat->mouse);
        qa_seat_settings_free(&seat->settings); qa_cvar_archive_free(&seat->client_archive); qa_cvar_archive_free(&seat->mouse_archive);
    }
    if (source->fallback!=source->movement) qa_cvars_destroy(source->fallback);
    qa_cvars_destroy(source->movement);
    qa_cvar_archive_free(&source->source_archive); qa_cvar_archive_free(&source->movement_archive); qa_cvar_archive_free(&source->fallback_archive);
    qa_launch_instance_lease_release(source->metadata); free(source); return true;
}
static bool prepare(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_launch_instance *selected,qa_console *console,qa_cvars *cvars,const qa_command_context *command,
    void **phase,qa_error *error)
{
    frontend_config_store *manager=context; qa_frontend *f=manager->frontend;
    if (!phase || *phase || !selected || !console || !cvars || !command || frontend_config_store_source(manager,console))
        return fail(error,QA_ERROR_ARGUMENT,"Source configuration requires its fresh actual console and registry");
    frontend_config_source *source=calloc(1,sizeof(*source));
    if (!source) return fail(error,QA_ERROR_MEMORY,"Retaining source configuration");
    source->manager=manager; source->application=application; source->candidate=candidate;
    source->console=console; source->cvars=cvars; source->command=*command;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    source->primary=entities && !strcmp(entities->instance,selected->selection.instance);
    qa_catalog *catalog=qa_launch_snapshot_catalog(candidate);
    const qa_product *product=qa_catalog_product(catalog,selected->selection.product);
    const qa_product *base=product && product->base?qa_catalog_product(catalog,product->base):product;
    source->has_mod=product && base && strcmp(product->directory,base->directory)!=0;
    bool ok=product && qa_launch_instance_retain_metadata(selected,&source->metadata,error);
    if (ok) source->files=frontend_config_files_create(catalog,product->id,f->options.application.user_root,
        f->options.application.content_root,error);
    ok=ok && source->files;
    if (ok && source->primary) ok=frontend_input_profile_bind_store(f,catalog,product->id,
        frontend_config_files_store(source->files,false),error);
    if (ok && product->family==QA_GAME_Q3) {
        qa_q3_product_policy policy;
        ok=f->keys && qa_application_q3_product_policy_read(application,&policy) &&
            frontend_keys_prepare(f->keys,source->files,cvars,&policy,f->options.dedicated,&source->keys,error);
        if (ok) ok=frontend_key_profile_scope(source->keys,
            (qa_application_console_scope){.provider=command->owner,.kind=QA_APPLICATION_CONSOLE_Q3_GAME},cvars,error);
        if (!ok && (!error || error->code==QA_OK)) fail(error,QA_ERROR_ARGUMENT,"Q3 configuration needs its genuine shared key and resolved initial policy owners");
    }
    const qa_launch_binding *movement=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER},QA_ROLE_MOVEMENT,"");
    const qa_launch_instance *movement_source=movement?qa_launch_snapshot_find(candidate,movement->instance):selected;
    source->movement_dialect=(qa_console_dialect)(movement_source?movement_source->selection.clock.kind:selected->selection.clock.kind);
    if (ok) source->movement=registry(source,source->movement_dialect,error);
    if (ok) source->fallback=source->movement_dialect==command->dialect?source->movement:registry(source,command->dialect,error);
    ok=ok && source->movement && source->fallback;
    qa_settings_store store=source->files?frontend_config_files_store(source->files,false):(qa_settings_store){0};
    const char *source_owner[3]={"source",product?product->key:NULL,selected->selection.implementation};
    const char *dialects[]={"q1-netquake","q1-quakeworld","q2-classic","q2-rerelease","q3"};
    if (ok && source->primary && !f->options.dedicated) {
        const char *movement_owner[2]={"movement",dialects[source->movement_dialect]};
        const char *fallback_owner[2]={"fallback",dialects[command->dialect]};
        ok=qa_settings_load_cvars(store,source_owner,3,command->dialect,&source->source_archive,error) &&
            qa_settings_load_cvars(input_store(source),movement_owner,2,source->movement_dialect,&source->movement_archive,error) &&
            qa_settings_load_cvars(input_store(source),fallback_owner,2,command->dialect,&source->fallback_archive,error);
    }
    for (unsigned i=0;ok && source->primary && !f->options.dedicated && i<f->options.seats;++i) {
        config_seat *seat=source->seats+source->seat_count++;
        seat->cvars=registry(source,command->dialect,error); seat->mouse=registry(source,command->dialect,error);
        qa_command_context seat_command=*command; seat_command.origin=QA_COMMAND_SEAT; seat_command.seat=i;
        qa_input_seat_options options={.context=seat_command,.console=console,.cvars=seat->mouse,.gamepad=qa_gamepad_defaults()};
        if (seat->cvars && seat->mouse) seat->input=qa_input_seat_create(&options,error);
        char index[16],path[64]; snprintf(index,sizeof(index),"%u",i); snprintf(path,sizeof(path),"input/seat-%u.json",i+1);
        const char *client_owner[4]={"client",product->key,selected->selection.implementation,index};
        const char *mouse_owner[3]={"input",dialects[command->dialect],index};
        ok=seat->input && qa_input_seat_profile(seat->input,source->movement_dialect,error) &&
            qa_input_settings_register(seat->mouse,(qa_movement_kind)source->movement_dialect,error) &&
            qa_settings_load_seat(input_store(source),path,&seat->settings,&seat->found,error) &&
            qa_settings_load_cvars(store,client_owner,4,command->dialect,&seat->client_archive,error) &&
            qa_settings_load_cvars(input_store(source),mouse_owner,3,command->dialect,&seat->mouse_archive,error);
    }
    if (ok && source->primary && source->seat_count) {
        qa_input_console_options input={.console=console,.owner=command->owner,.user=source,.seat=binding_seat,.print=print};
        source->bindings=qa_input_console_create(&input,error); ok=source->bindings!=NULL;
    }
    if (ok) { source->write_registered=qa_console_register_owned(console,"writeconfig","Save the actual source configuration",command->owner,
        command->owner,true,config_command,source,error); ok=source->write_registered; }
    if (ok) { source->dump_registered=qa_console_register_owned(console,"condump","Dump the actual local console buffer",command->owner,
        command->owner,true,config_command,source,error); ok=source->dump_registered; }
    if (ok && source->primary) ok=phase_create(source,error);
    if (!ok) { qa_error ignored={0}; (void)source_destroy(source,&ignored); return false; }
    source->next=manager->sources; manager->sources=source; *phase=source; return true;
}
static bool advance(void *context,void *phase,qa_console *console,bool *complete,qa_error *error)
{
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || source->console!=console || source->released || source->running || !complete)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration frame has another actual source owner");
    *complete=false;
    if (!source->primary) { source->configured=true; *complete=true; return true; }
    source->running=true; bool done=false;
    bool ok=frontend_startup_config_advance(source->phase,console,&done,error);
    source->running=false;
    if (!ok || !done) return ok;
    if (source->seat_count && source->seat_index+1<source->seat_count) {
        if (!frontend_startup_config_destroy(source->phase,error)) return false;
        source->phase=NULL; ++source->seat_index; source->configured=false;
        if (!defaults(source,error) || !phase_create(source,error)) return false;
        return true;
    }
    *complete=true; return true;
}
static bool phase_read(void *context,void *phase,const qa_command_context *command,const char *name,
    qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_source *source=phase;
    if (!source || source->manager!=context) return fail(error,QA_ERROR_ARGUMENT,"Script read has another configuration owner");
    return source->phase?frontend_startup_config_read(source->phase,command,name,bytes,lease,error):
        frontend_config_files_console_read(source->files,name,command,bytes,lease,error);
}
static void phase_release(void *context,void *phase,void *lease)
{
    frontend_config_source *source=phase;
    if (source && source->manager==context) {
        if (source->phase) frontend_startup_config_release(source->phase,lease); else release(source,lease);
    }
}
static void phase_complete(void *context,void *phase,const qa_command_context *command,const char *name,bool success)
{
    frontend_config_source *source=phase;
    if (source && source->manager==context && source->phase)
        frontend_startup_config_script_complete(source->phase,command,name,success);
}
static bool allow(void *context,void *phase,const qa_command_invocation *command)
{
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || !source_context(source,&command->context)) return false;
    if (!source->phase || !frontend_startup_config_restrict_shared(source->phase) || !command->argc) return true;
    const char *name=command->argv[0];
    if (equal(name,"cvar_restart")) { print(source,"Ignoring shared cvar restart in saved secondary-seat configuration.\n"); return false; }
    bool setter=equal(name,"set") || equal(name,"seta") || equal(name,"sets") || equal(name,"setu") ||
        equal(name,"toggle") || equal(name,"reset");
    const char *target=setter?(command->argc>1?command->argv[1]:NULL):name;
    if (!target) return true;
    qa_cvars *owner=frontend_config_store_cvar_owner(source->manager,source->console,&command->context,target);
    if (!setter && (!owner || !qa_cvars_find(owner,target))) return true;
    if (command->context.seat<source->seat_count &&
        (owner==source->seats[command->context.seat].cvars || owner==source->seats[command->context.seat].mouse)) return true;
    print(source,"Ignoring shared cvar in saved secondary-seat configuration; use autoexec.cfg for shared overrides.\n");
    return false;
}
static bool phase_destroy(void *context,void *phase,qa_error *error)
{
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || source->running) return fail(error,QA_ERROR_ARGUMENT,"Configuration phase is still executing");
    if (!frontend_startup_config_destroy(source->phase,error)) return false;
    source->phase=NULL; source->released=true; return true;
}
static bool prepare_candidate(void *context,qa_application *application,const qa_launch_snapshot *candidate,qa_error *error)
{
    frontend_config_store *manager=context; qa_frontend *f=manager->frontend;
    if (manager->prepared || manager->running || !candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration publication already has another actual candidate");
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(candidate,entities->instance):NULL;
    qa_console *primary_console=NULL; qa_cvars *primary_cvars=NULL; qa_command_context primary_command;
    if (!selected || !qa_application_startup_source_read(application,candidate,selected,
        &primary_console,&primary_cvars,&primary_command,error)) return false;
    frontend_config_source *primary=frontend_config_store_source(manager,primary_console);
    if (!primary || primary->application!=application || primary->cvars!=primary_cvars ||
        !source_context(primary,&primary_command))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration publication lost its exact physical primary source");
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        if (source->application!=application || source->published || source->candidate!=candidate) continue;
        if (!source->configured || source->running || !qa_console_idle(source->console) ||
            qa_console_pending(source->console) || !frontend_config_files_idle(source->files))
            return fail(error,QA_ERROR_ARGUMENT,"Configuration publication requires all returned actual source phases");
    }
    for (size_t i=0;!primary->published && i<primary->seat_count;++i) {
        config_seat *seat=primary->seats+i;
        qa_input_seat *active=f->seats && i<f->options.seats?f->seats[i].input:NULL;
        if (!active) return fail(error,QA_ERROR_ARGUMENT,"Configuration candidate has no actual stable local seat");
        int32_t controller=f->input?qa_input_platform_controller(f->input,(unsigned)i):-1;
        if ((controller>=0 && !qa_input_seat_remap_controller(seat->input,controller,error)) ||
            !qa_input_seat_configuration_ready(active,seat->input,error)) return false;
        seat->publication_input=active;
    }
    if (f->keys && !frontend_keys_publication_ready(f->keys,primary->keys,
        primary->keys?primary->cvars:NULL,&manager->key_publication,error)) return false;
    manager->prepared=candidate; manager->prepared_primary=primary; return true;
}
static bool preinit(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_launch_instance *selected,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command,qa_error *error)
{
    frontend_config_source *source=frontend_config_store_source(context,console);
    const qa_launch_instance *retained=source?instance(source):NULL;
    if (!source || source->application!=application || source->candidate!=candidate ||
        source->cvars!=cvars || !selected || !retained ||
        strcmp(retained->selection.instance,selected->selection.instance) ||
        !source->configured || !source->released || source->running ||
        !source_context(source,command))
        return fail(error,QA_ERROR_ARGUMENT,"Source Init requires its completed genuine configuration phase");
    return true;
}
static bool retire(void *context,qa_application *application,const qa_launch_instance *selected,
    qa_console *console,qa_cvars *cvars,qa_error *error)
{
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!source) return true;
    const qa_launch_instance *retained=instance(source);
    if (source->application!=application || source->cvars!=cvars || !selected || !retained ||
        strcmp(retained->selection.instance,selected->selection.instance))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration retirement names another actual source owner");
    return frontend_config_store_retire(context,console,error);
}
static qa_cvars *cvar_owner(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,const char *name)
{
    frontend_config_source *source=frontend_config_store_source(context,console);
    return source && source->application==application?
        frontend_config_store_cvar_owner(context,console,command,name):NULL;
}
static bool visible_cvars(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,size_t index,qa_cvars **out)
{
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!out || !source || source->application!=application || !source_context(source,command)) return false;
    *out=frontend_config_store_visible_cvars(context,console,command,index); return true;
}
static void finish(void *context,qa_application *application,const qa_launch_snapshot *candidate,bool published)
{
    frontend_config_store *manager=context;
    if (published) {
        for (frontend_config_source *source=manager->sources;source;source=source->next) {
            if (source->application!=application || source->published || source->candidate!=manager->prepared) continue;
            for (size_t i=0;i<source->seat_count;++i) {
                config_seat *seat=source->seats+i;
                qa_input_seat_configuration_publish(seat->publication_input,seat->input);
                seat->publication_input=NULL;
            }
            source->published=true; source->candidate=NULL;
        }
        for (frontend_config_source *source=manager->sources;source;source=source->next)
            if (source->application==application && source->published) source->primary=source==manager->prepared_primary;
        if (manager->key_publication.owner) frontend_keys_publication_publish(&manager->key_publication);
    } else {
        frontend_keys_publication_discard(&manager->key_publication);
        /* The checked physical source retirement follows while its console is
         * still alive, including candidates which never reached preflight. */
        for (frontend_config_source *source=manager->sources;source;source=source->next)
            if (source->application==application && !source->published &&
                (!candidate || source->candidate==candidate))
                for (size_t i=0;i<source->seat_count;++i) source->seats[i].publication_input=NULL;
    }
    manager->prepared=NULL; manager->prepared_primary=NULL;
}
frontend_config_store *frontend_config_store_create(qa_frontend *frontend,qa_error *error)
{
    if (!frontend) return fail(error,QA_ERROR_ARGUMENT,"Configuration manager needs its actual frontend owner"),NULL;
    frontend_config_store *manager=calloc(1,sizeof(*manager));
    if (!manager) return fail(error,QA_ERROR_MEMORY,"Retaining frontend configuration manager"),NULL;
    manager->frontend=frontend;
    manager->hooks=(qa_application_startup_hooks){.context=manager,.prepare_source=prepare,.advance_source=advance,
        .read_script=phase_read,.release_script=phase_release,.script_complete=phase_complete,
        .allow_command=allow,.prepare_candidate=prepare_candidate,.release_source=phase_destroy,.finish_candidate=finish,
        .preinit_source=preinit,.retire_source=retire,.cvar_owner=cvar_owner,.visible_cvars=visible_cvars};
    return manager;
}
const qa_application_startup_hooks *frontend_config_store_hooks(frontend_config_store *manager)
{ return manager?&manager->hooks:NULL; }
bool frontend_config_store_destroy(frontend_config_store *manager,qa_error *error)
{
    if (!manager) return true;
    if (manager->running || manager->prepared || manager->key_publication.owner)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration manager retains an executing or prepared candidate");
    while (manager->sources) {
        frontend_config_source *source=manager->sources; frontend_config_source *next=source->next;
        if (!source_destroy(source,error)) return false;
        manager->sources=next;
    }
    free(manager); return true;
}
bool frontend_config_store_read(frontend_config_store *manager,const qa_console *console,const qa_command_context *command,
    const char *name,qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_source *source=frontend_config_store_source(manager,console);
    return source_context(source,command) && frontend_config_files_console_read(source->files,name,command,bytes,lease,error);
}
void frontend_config_store_release(frontend_config_store *manager,const qa_console *console,void *lease)
{
    frontend_config_source *source=frontend_config_store_source(manager,console);
    if (source) frontend_config_files_release(source->files,lease);
}
bool frontend_config_store_save(frontend_config_store *manager,qa_error *error)
{
    if (!manager || manager->running) return fail(error,QA_ERROR_ARGUMENT,"Configuration archive requires its returned source owners");
    for (frontend_config_source *source=manager->sources;source;source=source->next) if (source->published && source->primary) {
        qa_command_context command;
        if (!current_command(source,&command,error)) return false;
        const qa_launch_instance *selected=instance(source);
        const qa_product *product=qa_catalog_product(frontend_config_files_catalog(source->files),selected->selection.product);
        const char *owner[3]={"source",product->key,selected->selection.implementation};
        const char *dialects[]={"q1-netquake","q1-quakeworld","q2-classic","q2-rerelease","q3"};
        const char *movement_owner[2]={"movement",dialects[source->movement_dialect]};
        const char *fallback_owner[2]={"fallback",dialects[source->command.dialect]};
        qa_settings_store store=frontend_config_files_store(source->files,false),input=input_store(source);
        if (!qa_settings_save_cvars(store,owner,3,source->cvars,error) ||
            !qa_settings_save_cvars(input,movement_owner,2,source->movement,error) ||
            !qa_settings_save_cvars(input,fallback_owner,2,source->fallback,error)) return false;
        for (size_t i=0;i<source->seat_count;++i) {
            config_seat *seat=source->seats+i;
            char index[16],path[64]; snprintf(index,sizeof(index),"%u",(unsigned)i);
            snprintf(path,sizeof(path),"input/seat-%u.json",(unsigned)i+1);
            const char *client_owner[4]={"client",product->key,selected->selection.implementation,index};
            const char *mouse_owner[3]={"input",dialects[source->command.dialect],index};
            qa_input_seat *active=manager->frontend->seats && i<manager->frontend->options.seats?
                manager->frontend->seats[i].input:NULL;
            if (!active) return fail(error,QA_ERROR_ARGUMENT,"Source input archive lost its actual published seat");
            size_t count=qa_input_seat_binding_count(active);
            if (count>SIZE_MAX/sizeof(qa_input_binding)) return fail(error,QA_ERROR_MEMORY,"Source input archive exceeds binding storage");
            qa_input_binding *bindings=count?malloc(count*sizeof(*bindings)):NULL;
            if (count && !bindings) return fail(error,QA_ERROR_MEMORY,"Retaining actual live input bindings for archive");
            for (size_t n=0;n<count;++n) bindings[n]=*qa_input_seat_binding_at(active,n);
            qa_input_command_tuning tuning;
            bool ok=qa_input_settings_read(seat->mouse,(qa_movement_kind)source->movement_dialect,&tuning,error);
            qa_seat_settings settings=seat->settings;
            if (ok) {
                settings.bindings=bindings; settings.binding_count=count;
                settings.mouse=tuning.mouse; settings.gamepad=*qa_input_seat_gamepad_tuning(active);
                settings.has_always_run=true; settings.always_run=tuning.view.always_run;
                ok=qa_settings_save_cvars(store,client_owner,4,seat->cvars,error) &&
                    qa_settings_save_cvars(input,mouse_owner,3,seat->mouse,error) &&
                    qa_settings_save_seat(input,path,&settings,error);
            }
            free(bindings); if (!ok) return false;
        }
    }
    return true;
}
bool frontend_config_store_retire(frontend_config_store *manager,const qa_console *console,qa_error *error)
{
    if (!manager || manager->running) return fail(error,QA_ERROR_ARGUMENT,"Configuration retirement requires returned source callbacks");
    frontend_config_source **at=&manager->sources;
    while (*at && (*at)->console!=console) at=&(*at)->next;
    if (!*at) return true;
    frontend_config_source *source=*at,*next=source->next;
    if (!source_destroy(source,error)) return false;
    *at=next; return true;
}
void frontend_config_store_rebind(frontend_config_store *manager,qa_frontend *frontend)
{ if (manager && !manager->running) manager->frontend=frontend; }
