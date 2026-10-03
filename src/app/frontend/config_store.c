#include "config_store.h"
#include "commands.h"
#include "input_profile.h"
#include "config_userinfo.h"
#include "config_bindings.h"
#include "client_registry.h"
#include "authored_bindings.h"
#include "config_weapon_defaults.h"
#include "remote_config.h"
#include "neutral_config.h"
#include "legacy_render_policy.h"
#include "qa/source_frame_time.h"
#include "network_config.h"
#include "network_admin.h"
#include "source_admin.h"
#include "network_qw_logfile.h"
#include "native_q3_client.h"
#include "selected_effects.h"
#include "shared_settings.h"
#include "shared_publication.h"
#include "shared_storage.h"
#include "view_settings.h"
#include "music_sources.h"
#include "global_settings_storage.h"
#include "save_private.h"
#include "qa/cvars_save.h"
#include "qa/console_cvar_observer.h"
#include "qa/console_save.h"
#include "qa/console_program.h"
#include "qa/catalog_save.h"
#include "qa/text.h"
#include "qa/server_admin.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct config_seat {
    qa_input_seat *input,*publication_input;
    qa_seat_console *publication_console;
    qa_command_context publication_command;
    uint32_t logical;
    qa_console_dialect movement_dialect;
    char *registry_instance;
    qa_cvars *cvars,*mouse;
    frontend_client_registry *registry;
    frontend_authored_bindings *authored;
    qa_seat_settings settings;
    qa_cvar_archive client_archive,mouse_archive;
    bool found,cvars_transferred,registry_bound;
} config_seat;
struct frontend_config_source {
    frontend_config_source *next;
    frontend_config_store *manager;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_launch_instance_lease *metadata;
    char *saved_instance;
    qa_sha256_digest saved_identity;
    qa_console *console;
    qa_cvars *cvars,*movement,*fallback;
    frontend_config_files *files;
    frontend_key_profile *keys;
    frontend_startup_config *phase;
    qa_input_console *bindings;
    frontend_config_bindings *dedicated_bindings;
    qa_command_context command;
    qa_application_console_scope scope;
    uint64_t declaration_owner;
    qa_cvar_archive source_archive,movement_archive,fallback_archive;
    config_seat seats[QA_INPUT_LOCAL_SEATS];
    size_t seat_count,seat_index,registry_references,admin_registered;
    qa_console_dialect movement_dialect;
    bool primary,published,configured,released,running,write_registered,dump_registered,frag_registered,has_mod;
    bool imported;
    bool profile_carried,variables_carried,initial_variables;
    qa_error observation_failure;
};
typedef struct config_variable_carry {
    struct config_variable_carry *next;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    const void *storage;
    qa_console *console;
    qa_cvars *cvars;
} config_variable_carry;
struct frontend_config_store {
    qa_frontend *frontend;
    qa_application_startup_hooks hooks;
    frontend_config_source *sources;
    frontend_config_source *parked, *parked_from;
    qa_console *parked_console, *parked_root;
    qa_cvars *parked_cvars;
    qa_console_program *parked_program;
    qa_application_startup_source parked_basis;
    uint32_t parked_physical;
    bool parked_live, parked_local, parked_sealed;
    frontend_remote_configs *clients;
    frontend_neutral_configs *neutral;
    frontend_source_admin *admin;
    frontend_qw_logfile *qw_logfile;
    char *admin_source;
    qa_sha256_digest admin_identity;
    config_variable_carry *variable_carries;
    frontend_config_source *prepared_primary;
    frontend_config_source *input_source;
    const qa_launch_snapshot *input_candidate;
    qa_application *input_application;
    config_seat input_seats[QA_INPUT_LOCAL_SEATS];
    size_t input_count;
    bool input_prepared;
    const qa_launch_snapshot *prepared;
    qa_application *prepared_application;
    frontend_keys_publication key_publication;
    frontend_keys_cvar_refs restore_refs;
    frontend_shared_settings *shared;
    frontend_shared_publication *publication;
    qa_application *shared_application;
    const qa_launch_snapshot *shared_candidate;
    frontend_shared_storage *storage;
    qa_buffer restored_storage;
    qa_application_content_graph *storage_graph;
    qa_console *images_console;
    qa_console *root_console;
    qa_cvars *root_cvars;
    qa_command_context root_command;
    frontend_startup_config *images_program;
    qa_application_startup_source images_source;
    qa_cvar_archive shared_archive;
    bool storage_seeded,shared_seeded,shared_archived,images_audio_seeded;
    bool image_fov_touched,images_fov_touched,sticky_seed_pending;
    bool restoring;
    bool running;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool refresh_source(void *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
static bool startup_source(void *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,bool *,qa_error *);
static bool shared_program_destroy(frontend_config_store *,qa_error *);
static bool images_prepare(frontend_config_store *,const qa_application_startup_source *,qa_error *);
static bool shared_storage_prepare(frontend_config_store *,qa_error *);
static bool prepare_retained_input(frontend_config_store *,qa_application *,const qa_launch_snapshot *,qa_error *);
static void discard_retained_input(frontend_config_store *);
static bool equal(const char *left,const char *right)
{
    for (;;++left,++right) {
        unsigned a=(unsigned char)*left,b=(unsigned char)*right;
        if (a>='A' && a<='Z') a+='a'-'A';
        if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false;
        if (!a) return true;
    }
}
static bool parked_capture(void *,const qa_command_context *,qa_command_context *,qa_error *);
static bool parked_active(void *,const qa_command_context *);
static bool parked_seal(frontend_config_store *,frontend_config_source *,qa_error *);
static const qa_launch_instance *instance(const frontend_config_source *source)
{ return qa_launch_instance_lease_view(source->metadata); }
static bool game_scope(qa_application_console_scope scope)
{
    return !scope.seat && (scope.kind==QA_APPLICATION_CONSOLE_QC ||
        scope.kind==QA_APPLICATION_CONSOLE_NATIVE_Q2 || scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME ||
        scope.kind==QA_APPLICATION_CONSOLE_Q1_GAME || scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME);
}
static bool same_scope(qa_application_console_scope a,qa_application_console_scope b)
{ return a.provider==b.provider && a.kind==b.kind && a.seat==b.seat; }
static bool client_scope(qa_application_console_scope scope)
{ return scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME || scope.kind==QA_APPLICATION_CONSOLE_Q3_UI; }
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
static bool cheats_allowed(void *context)
{
    frontend_config_source *source=context;
    const qa_cvar_view *value=source->command.dialect==QA_CONSOLE_Q3?
        frontend_config_store_engine_value(source->manager,source->application,source->console,"sv_cheats"):
        qa_cvars_find(source->cvars,"sv_cheats");
    return value && value->number==1;
}
static qa_cvar_options registry_options(frontend_config_source *source,qa_console_dialect dialect)
{
    return (qa_cvar_options){.dialect=dialect,.user=source,.print=print,.cheats_allowed=cheats_allowed};
}
static qa_cvars *registry(frontend_config_source *source,qa_console_dialect dialect,qa_error *error)
{
    qa_cvar_options options=registry_options(source,dialect);
    return qa_cvars_create(&options,error);
}
static bool source_context(const frontend_config_source *source,const qa_command_context *command)
{
    return source && command && command->origin!=QA_COMMAND_REMOTE &&
        command->owner==source->command.owner && command->session==source->command.session &&
        command->dialect==source->command.dialect &&
        (source->manager->parked==source?parked_active(source->manager,command):
            qa_application_command_context_active(source->application,command));
}
static bool source_cvar_context(const frontend_config_source *source,const qa_command_context *command)
{
    if (source_context(source,command)) return true;
    return source && command && !source->imported && source->configured && source->released &&
        instance(source) && source->console && source->cvars &&
        qa_console_cvars(source->console)==source->cvars && command->origin!=QA_COMMAND_REMOTE &&
        command->owner==source->command.owner && command->session==source->command.session &&
        command->dialect==source->command.dialect && qa_console_cvar_entered(source->console,command);
}
static bool binding_context(void *context,const qa_command_context *command)
{ return source_context(context,command); }
static bool current_command(const frontend_config_source *source,qa_command_context *command,qa_error *error)
{
    *command=source->command;
    command->registry=0; command->generation=0; command->actor=(qa_actor_id){0};
    return (source->manager->parked==source?parked_capture(source->manager,command,command,error):
        qa_application_capture_command_context(source->application,command,command,error)) &&
        source_context(source,command);
}
static size_t seat_index(const frontend_config_source *source,uint32_t logical)
{
    for (size_t i=0;i<source->seat_count;++i) if (source->seats[i].logical==logical) return i;
    return source->seat_count;
}
static bool seat_movement(const qa_launch_snapshot *snapshot,uint32_t logical,
    qa_console_dialect *dialect,qa_error *error)
{
    const qa_launch_binding *binding=qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=logical},QA_ROLE_MOVEMENT,"");
    const qa_launch_instance *selected=binding?qa_launch_snapshot_find(snapshot,binding->instance):NULL;
    if (!selected || selected->selection.clock.kind>QA_CLOCK_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Input configuration lacks its actual selected seat movement source");
    *dialect=(qa_console_dialect)selected->selection.clock.kind; return true;
}
static bool same_command(const qa_command_context *,const qa_command_context *);
static bool input_context(void *context,uint32_t ordinal,const qa_command_context *command,qa_error *error)
{
    frontend_config_source *source=context;
    if (!source || !command || ordinal>=source->seat_count || command->origin!=QA_COMMAND_SEAT ||
        command->seat!=source->seats[ordinal].logical)
        return fail(error,QA_ERROR_ARGUMENT,"Prepared input context leaves its actual source and authored seat");
    if (source_context(source,command)) return true;
    const config_seat *seat=source->seats+ordinal;
    if (seat->input && qa_input_seat_ordinal(seat->input)==ordinal &&
        command->dialect==seat->movement_dialect && command->owner==source->command.owner &&
        command->session==source->command.session && command->client==source->command.client &&
        command->direct==source->command.direct && !command->script && !command->console_text &&
        command->registry==source->command.registry && command->generation==source->command.generation &&
        qa_application_command_context_active(source->application,command)) {
        qa_command_context actual=qa_input_seat_context(seat->input);
        if (same_command(command,&actual)) return true;
    }
    return fail(error,QA_ERROR_ARGUMENT,"Prepared input context leaves its actual source and authored seat");
}
frontend_config_source *frontend_config_store_source(const frontend_config_store *owner,const qa_console *console)
{
    if (owner && console) for (frontend_config_source *source=owner->sources;source;source=source->next)
        if (source->console==console) return source;
    return NULL;
}
frontend_remote_config *frontend_config_store_client(const frontend_config_store *owner,const qa_console *console)
{ return owner?frontend_remote_config_find(owner->clients,console):NULL; }
bool frontend_config_store_source_pending(const frontend_config_store *owner,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *authority)
{
    if (!owner || !authority) return false;
    frontend_remote_config *client=frontend_config_store_client(owner,authority->console);
    if (client) return frontend_remote_config_pending(client,application,candidate,authority);
    frontend_config_source *source=frontend_config_store_source(owner,authority->console);
    const qa_launch_instance *held=source?instance(source):NULL;
    return source && authority->descriptor && held && !source->published && !source->imported &&
        source->application==application && source->candidate==candidate && source->cvars==authority->cvars &&
        same_scope(source->scope,authority->scope) && held->storage==authority->descriptor->storage;
}
bool frontend_config_store_shared_begin(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    if (!manager || manager->restoring || (!manager->shared && manager->images_source.descriptor) || (manager->shared &&
        (manager->shared_application!=application || manager->shared_candidate!=candidate)))
        return fail(error,QA_ERROR_ARGUMENT,"Shared settings already retain another actual candidate");
    bool ok=frontend_shared_settings_begin(manager->frontend,manager,application,candidate,source,&manager->shared,error);
    if (manager->shared) { manager->shared_application=application; manager->shared_candidate=candidate; }
    return ok && images_prepare(manager,source,error);
}
bool frontend_config_store_shared_pending(const frontend_config_store *manager)
{
    return manager && manager->shared && manager->shared_application==manager->frontend->application &&
        frontend_config_store_shared(manager,manager->shared_application,manager->shared_candidate);
}
frontend_shared_settings *frontend_config_store_shared(const frontend_config_store *manager,
    const qa_application *application,const qa_launch_snapshot *candidate)
{
    return manager && manager->shared_application==application && manager->shared_candidate==candidate &&
        frontend_shared_settings_current(manager->shared,manager->frontend,application,candidate)?manager->shared:NULL;
}
static bool shared_consumed(const frontend_config_store *manager,qa_application *application)
{
    return manager && manager->shared_application==application &&
        frontend_shared_settings_consumed_is(manager->shared,manager->frontend,application,manager->shared_candidate);
}
static bool root_current(const frontend_config_store *manager)
{
    qa_console *console=NULL; qa_cvars *registry=NULL; qa_command_context command;
    return manager && manager->root_console && manager->shared_application &&
        qa_application_startup_root_read(manager->shared_application,manager->shared_candidate,
            &console,&registry,&command,NULL) && console==manager->root_console && registry==manager->root_cvars &&
        command.owner==manager->root_command.owner && command.session==manager->root_command.session &&
        command.client==manager->root_command.client && command.seat==manager->root_command.seat &&
        command.origin==manager->root_command.origin && command.dialect==manager->root_command.dialect &&
        command.registry==manager->root_command.registry && command.generation==manager->root_command.generation &&
        command.console_text==manager->root_command.console_text && command.script==manager->root_command.script &&
        command.direct==manager->root_command.direct &&
        qa_actor_id_equal(command.actor,manager->root_command.actor);
}
static bool images_phase(const frontend_config_store *manager)
{
    return manager->root_console?root_current(manager) &&
        qa_application_startup_root_phase(manager->shared_application,manager->shared_candidate):
        qa_application_startup_images_phase(manager->shared_application,manager->shared_candidate,&manager->images_source);
}
static const qa_command_context *images_basis(const frontend_config_store *manager)
{ return manager->root_console?&manager->root_command:&manager->images_source.command; }
bool frontend_config_store_images_command_current(const frontend_config_store *manager,
    const qa_application *application,const qa_launch_snapshot *candidate,
    const qa_console *console,const qa_command_context *command)
{
    return manager && console && console==manager->images_console && command &&
        application==manager->shared_application && candidate==manager->shared_candidate &&
        frontend_config_store_shared(manager,application,candidate) &&
        (manager->root_console?root_current(manager):frontend_config_store_source_pending(manager,manager->shared_application,
            manager->shared_candidate,&manager->images_source)) && images_phase(manager) &&
        qa_application_command_context_active(manager->shared_application,command) &&
        frontend_startup_images_command_current(manager->images_program,console,command);
}
bool frontend_config_store_images_pending(const frontend_config_store *manager)
{
    frontend_shared_settings *owner=manager?frontend_config_store_shared(manager,
        manager->shared_application,manager->shared_candidate):NULL;
    frontend_shared_values *values=frontend_shared_settings_values(owner);
    return manager && !manager->shared_seeded && manager->images_console && manager->images_program &&
        owner && qa_cvars_edit_returned_is(frontend_shared_values_prepared(values),frontend_shared_values_registry(values)) &&
        images_phase(manager) &&
        qa_console_idle(manager->images_console) &&
        !frontend_startup_images_completed(manager->images_program,manager->images_console);
}
const frontend_shared_storage *frontend_config_store_shared_storage(const frontend_config_store *manager)
{ return manager?manager->storage:NULL; }
static bool shared_storage_current(const frontend_config_store *manager)
{
    if (!manager || !manager->storage) return false;
    const qa_frontend *f=manager->frontend;
    return frontend_global_settings_storage_idle(f->global_settings_storage) &&
        frontend_shared_storage_current(manager->storage,
            frontend_global_settings_storage_user_store(f->global_settings_storage),
            frontend_global_settings_storage_device_store(f->global_settings_storage),
            frontend_config_store_input_store(manager),!f->options.dedicated);
}
static bool shared_storage_prepare(frontend_config_store *manager,qa_error *error)
{
    qa_frontend *f=manager->frontend;
    if (!frontend_global_settings_storage_idle(f->global_settings_storage))
        return fail(error,QA_ERROR_ARGUMENT,"Shared persistence lost its actual global directory owner");
    qa_settings_store user=frontend_global_settings_storage_user_store(f->global_settings_storage);
    qa_settings_store devices=frontend_global_settings_storage_device_store(f->global_settings_storage);
    qa_settings_store input=frontend_config_store_input_store(manager);
    bool graphical=!manager->frontend->options.dedicated;
    if (!manager->storage && !frontend_shared_storage_open(user,devices,input,graphical,&manager->storage,error)) return false;
    qa_settings_store previous={0};
    if (!frontend_shared_storage_input(manager->storage,&previous))
        return fail(error,QA_ERROR_ARGUMENT,"Shared persistence lost its admitted sticky store receipt");
    if (input.vfs) {
        if (!frontend_shared_storage_adopt_input(manager->storage,input,error)) return false;
        if (!previous.vfs && manager->storage_seeded) manager->sticky_seed_pending=true;
    }
    if (!frontend_shared_storage_current(manager->storage,user,devices,input,graphical))
        return fail(error,QA_ERROR_ARGUMENT,"Shared persistence changed its actual retained user or input root");
    return true;
}
static bool images_prepare(frontend_config_store *manager,const qa_application_startup_source *source,qa_error *error)
{
    if (!shared_storage_prepare(manager,error)) return false;
    if (!manager->root_console && !manager->images_source.descriptor) manager->images_source=*source;
    return true;
}
static bool images_context(void *context,const qa_command_context *command)
{
    frontend_config_store *manager=context;
    const qa_command_context *basis=images_basis(manager);
    return command && frontend_config_store_shared(manager,manager->shared_application,manager->shared_candidate) &&
        images_phase(manager) &&
        command->session==basis->session && command->owner==basis->owner && command->client==basis->client &&
        command->seat==basis->seat && command->origin==basis->origin && command->dialect==basis->dialect &&
        command->registry==basis->registry && command->generation==basis->generation &&
        command->console_text==basis->console_text && qa_actor_id_equal(command->actor,basis->actor) &&
        qa_application_command_context_active(manager->shared_application,command);
}
static bool images_access(frontend_config_store *manager,const qa_command_context *command,
    qa_cvars **registry,qa_cvars_edit **edit,qa_error *error)
{
    return manager->shared && frontend_shared_values_programme_access(
        frontend_shared_settings_values(manager->shared),manager->images_console,command,registry,edit,error);
}
static qa_cvars *images_owner(void *context,const qa_command_context *command,const char *name)
{
    (void)name;
    qa_cvars *registry=NULL; qa_cvars_edit *edit=NULL; qa_error error={0};
    return images_access(context,command,&registry,&edit,&error)?registry:NULL;
}
static qa_cvars *images_visible(void *context,const qa_command_context *command,size_t index)
{ return index?NULL:images_owner(context,command,NULL); }
static bool images_edit(void *context,const qa_command_context *command,qa_cvars *registry,
    qa_cvars_edit **out,qa_error *error)
{
    qa_cvars *actual=NULL;
    if (!images_access(context,command,&actual,out,error)) return false;
    return actual==registry || fail(error,QA_ERROR_ARGUMENT,"Image command lost its canonical prepared registry");
}
static void images_print(void *context,const qa_command_context *command,const char *text)
{ frontend_console_print(((frontend_config_store *)context)->frontend,command,text); }
static bool archive_remember(qa_cvar_archive *archive,const char *name,const char *value,qa_error *error)
{
    size_t index=0;
    while (index<archive->count && strcmp(archive->entries[index].name,name)) ++index;
    size_t length=strlen(value);
    char *copy=malloc(length+1);
    if (!copy) return fail(error,QA_ERROR_MEMORY,"Retaining canonical shared archive value");
    memcpy(copy,value,length+1);
    if (index==archive->count) {
        if (archive->count>=SIZE_MAX/sizeof(*archive->entries)) {
            free(copy); return fail(error,QA_ERROR_MEMORY,"Canonical shared archive exceeds storage");
        }
        char *key=malloc(strlen(name)+1);
        if (!key) { free(copy); return fail(error,QA_ERROR_MEMORY,"Retaining canonical shared archive name"); }
        strcpy(key,name);
        qa_cvar_archive_entry *entries=realloc(archive->entries,(archive->count+1)*sizeof(*entries));
        if (!entries) { free(key); free(copy); return fail(error,QA_ERROR_MEMORY,"Growing canonical shared archive"); }
        archive->entries=entries; archive->entries[archive->count++]=(qa_cvar_archive_entry){key,copy};
    } else { free(archive->entries[index].value); archive->entries[index].value=copy; }
    return true;
}
static bool images_observe(void *context,const qa_command_invocation *command,bool success,qa_error *error)
{
    (void)success;
    frontend_config_store *manager=context;
    if (!command || command->console!=manager->images_console ||
        !qa_console_invocation_current(command->console,command))
        return fail(error,QA_ERROR_ARGUMENT,"Image archive observer lost its actual tokenized invocation");
    qa_cvars *registry=NULL; qa_cvars_edit *edit=NULL;
    if (!images_access(manager,&command->context,&registry,&edit,error)) return false;
    if (!command->argc) return true;
    const char *name=(!strcmp(command->argv[0],"set") || !strcmp(command->argv[0],"seta"))?
        command->argc>1?command->argv[1]:NULL:command->argv[0];
    const qa_cvar_view *row=name?qa_cvars_edit_canonical_record(edit,name):NULL;
    const char *value=row && (row->flags&QA_CVAR_ARCHIVE)?row->value:NULL;
    if (value && row==qa_cvars_edit_canonical_record(edit,"fov")) manager->images_fov_touched=true;
    return !value || archive_remember(&manager->shared_archive,row->name,value,error);
}
static bool images_output(frontend_config_store *manager,frontend_shared_values *values,qa_error *error)
{
    const frontend_shared_audio_preferences *audio=frontend_shared_storage_audio(manager->storage);
    const qa_cvar_archive *archive=frontend_shared_storage_archive(manager->storage);
    for (size_t i=0;audio && audio->present && archive && i<archive->count;++i) {
        const qa_cvar_archive_entry *row=archive->entries+i;
        if (strcmp(row->name,"s_outputRate") && strcmp(row->name,"s_outputBits") && strcmp(row->name,"s_outputChannels")) continue;
        qa_cvar_archive one={.entries=(qa_cvar_archive_entry *)row,.count=1};
        if (!frontend_shared_values_archive(values,&one,error)) return false;
    }
    return true;
}
static bool images_view(frontend_config_store *manager,frontend_shared_values *values,bool preserve_touched,qa_error *error)
{
    const frontend_shared_view_preferences *view=frontend_shared_storage_view(manager->storage);
    if (manager->frontend->options.dedicated || !view || !view->present) return true;
    char value[64];
    if (!qa_format_ecmascript_number(view->field_of_view,value,error)) return false;
    qa_cvar_archive_entry row={(char *)"fov",value};
    qa_cvar_archive loaded={.entries=&row,.count=1};
    if (!frontend_shared_values_archive(values,&loaded,error)) return false;
    const qa_cvar_view *canonical=qa_cvars_edit_canonical_record(frontend_shared_values_prepared(values),"fov");
    if (!canonical) return fail(error,QA_ERROR_ARGUMENT,"Loaded view lost its actual canonical prepared fov");
    return preserve_touched || archive_remember(&manager->shared_archive,canonical->name,value,error);
}
static bool advance_images(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    bool *complete,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !complete) return fail(error,QA_ERROR_ARGUMENT,"Images frame requires its actual manager");
    *complete=false;
    if (!manager->shared) { *complete=true; return true; }
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    if (!owner || !manager->storage || !images_phase(manager))
        return fail(error,QA_ERROR_ARGUMENT,"Images frame lost its actual candidate and linked source");
    if (manager->shared_seeded) { *complete=true; return true; }
    frontend_shared_values *values=frontend_shared_settings_values(owner);
    const qa_cvars_edit *edit=frontend_shared_values_prepared(values);
    qa_cvars *registry=frontend_shared_values_registry(values);
    if (!qa_cvars_edit_returned_is(edit,registry))
        return fail(error,QA_ERROR_ARGUMENT,"Images snapshot requires its returned canonical edit");
    if (!manager->storage_seeded && !manager->frontend->options.dedicated) {
        if (!manager->images_audio_seeded) {
            if (!images_output(manager,values,error)) return false;
            manager->images_audio_seeded=true;
        }
        if (!manager->images_console) {
            qa_console_options options={.context=*images_basis(manager),.cvars=registry,.user=manager,
                .print=images_print,.cvar_owner=images_owner,.visible_cvars=images_visible,.cvar_edit=images_edit,
                .context_active=images_context,.post_dispatch=images_observe};
            manager->images_console=qa_console_create(&options,error);
            if (!manager->images_console) return false;
        }
        if (!manager->images_program) {
            manager->images_program=frontend_startup_images_create(images_basis(manager),
                frontend_shared_storage_images(manager->storage),error);
            if (!manager->images_program) return false;
        }
        bool done=false;
        if (!frontend_startup_config_advance(manager->images_program,manager->images_console,&done,error)) return false;
        if (!done) return true;
        if (!frontend_startup_images_completed(manager->images_program,manager->images_console))
            return fail(error,QA_ERROR_ARGUMENT,"Image archive precedes its actual completed programme");
        if (!images_output(manager,values,error)) return false;
    } else if (manager->storage_seeded) {
        for (size_t i=0;i<qa_cvars_edit_count(edit);++i) {
            const qa_cvar_view *row=qa_cvars_edit_at(edit,i);
            const char *value=qa_cvars_edit_archive_value(edit,row);
            if (value && !archive_remember(&manager->shared_archive,row->name,value,error)) return false;
        }
    }
    if (!manager->storage_seeded) {
        const qa_cvar_archive *archive=frontend_shared_storage_archive(manager->storage);
        for (size_t i=0;archive && i<archive->count;++i)
            if (!archive_remember(&manager->shared_archive,archive->entries[i].name,archive->entries[i].value,error)) return false;
        if (!images_view(manager,values,manager->images_fov_touched,error)) return false;
    } else if (frontend_remote_configs_fresh_input(manager->clients,application,candidate)) {
        qa_cvar_archive devices={0};
        if (!frontend_shared_storage_load_devices(manager->storage,&devices,error)) return false;
        bool ok=true;
        for (size_t i=0;ok && i<devices.count;++i)
            ok=archive_remember(&manager->shared_archive,devices.entries[i].name,devices.entries[i].value,error);
        qa_cvar_archive_free(&devices);
        if (!ok) return false;
    }
    if (manager->storage_seeded && manager->sticky_seed_pending && !manager->frontend->options.dedicated) {
        if (!images_output(manager,values,error)) return false;
        const qa_cvar_archive *archive=frontend_shared_storage_archive(manager->storage);
        for (size_t i=0;archive && i<archive->count;++i) {
            const qa_cvar_archive_entry *row=archive->entries+i;
            if (strcmp(row->name,"s_outputRate") && strcmp(row->name,"s_outputBits") &&
                strcmp(row->name,"s_outputChannels") && strcmp(row->name,"volume") &&
                strcmp(row->name,"bgmvolume") && strcmp(row->name,"music_shuffle") &&
                strcmp(row->name,"music_menu_track")) continue;
            if (!archive_remember(&manager->shared_archive,row->name,row->value,error)) return false;
        }
        if (!images_view(manager,values,manager->image_fov_touched,error)) return false;
    }
    if (!candidate && manager->root_console) {
        if (!root_current(manager) || qa_application_launch(application) ||
            !frontend_shared_values_archive(values,&manager->shared_archive,error)) return false;
        manager->shared_archived=true;
    }
    manager->shared_seeded=true; *complete=true; return true;
}
static bool shared_program_destroy(frontend_config_store *manager,qa_error *error)
{
    if (!qa_console_destroy_ready(manager->images_console))
        return fail(error,QA_ERROR_ARGUMENT,"Image programme still retains an entered command or console lease");
    if (!frontend_startup_config_destroy(manager->images_program,error)) return false;
    manager->images_program=NULL;
    qa_console_destroy(manager->images_console); manager->images_console=NULL;
    manager->images_source=(qa_application_startup_source){0};
    manager->root_console=NULL; manager->root_cvars=NULL; manager->root_command=(qa_command_context){0};
    qa_cvar_archive_free(&manager->shared_archive);
    manager->shared_seeded=manager->shared_archived=manager->images_audio_seeded=manager->images_fov_touched=false;
    return true;
}
bool frontend_config_store_images_release(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,qa_error *error)
{
    if (!manager || !manager->shared_seeded || !frontend_config_store_shared(manager,application,candidate) ||
        !qa_application_startup_resource_phase(application,candidate))
        return fail(error,QA_ERROR_ARGUMENT,"Image programme release requires its actual completed candidate phase");
    if (!!manager->images_program!=!!manager->images_console ||
        (manager->images_program && !frontend_startup_images_completed(manager->images_program,manager->images_console)) ||
        !qa_console_destroy_ready(manager->images_console))
        return fail(error,QA_ERROR_ARGUMENT,"Image programme has not returned its complete physical owner");
    if (!frontend_startup_config_destroy(manager->images_program,error)) return false;
    manager->images_program=NULL;
    qa_console_destroy(manager->images_console); manager->images_console=NULL;
    return true;
}
bool frontend_config_store_shared_cancel_advance(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!manager || !complete || !application || manager->frontend->application!=application)
        return fail(error,QA_ERROR_ARGUMENT,"Shared cancellation requires its actual frontend application");
    if (!manager->shared) {
        if (manager->shared_application || manager->shared_candidate)
            return fail(error,QA_ERROR_ARGUMENT,"Shared cancellation retains a stale candidate association");
        *complete=true; return true;
    }
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    return owner?frontend_shared_settings_cancel_advance(owner,complete,error):
        fail(error,QA_ERROR_ARGUMENT,"Shared cancellation names another retained candidate preparation");
}
bool frontend_config_store_shared_engine_shutdown(frontend_config_store *manager,
    const qa_application_engine_shutdown *loan,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!manager || !complete || !manager->shared || !loan ||
        qa_application_engine_shutdown_owner(loan)!=manager->shared_application ||
        qa_application_engine_shutdown_candidate(loan)!=manager->shared_candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Shared shutdown requires its exact retained candidate ENGINE loan");
    if (!shared_program_destroy(manager,error)) return false;
    bool ok=frontend_shared_settings_engine_shutdown(&manager->shared,loan,complete,error);
    if (!manager->shared) { manager->shared_application=NULL; manager->shared_candidate=NULL; }
    return ok;
}
static bool pending_tuple(const frontend_config_store *manager,qa_application *application,
    const qa_console *console,qa_application_startup_source *out)
{
    if (!manager || !out || !console || application!=manager->shared_application || !manager->shared_candidate) return false;
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client) return frontend_remote_config_tuple(client,out) &&
        frontend_config_store_source_pending(manager,application,manager->shared_candidate,out);
    frontend_config_source *source=frontend_config_store_source(manager,console);
    if (!source || !instance(source)) return false;
    *out=(qa_application_startup_source){instance(source),source->scope,source->console,source->cvars,
        source->command,source->declaration_owner};
    return frontend_config_store_source_pending(manager,application,manager->shared_candidate,out);
}
const qa_cvar_view *frontend_config_store_engine_value(const frontend_config_store *manager,
    qa_application *application,const qa_console *console,const char *name)
{
    if (!manager || !application || application!=manager->frontend->application || !console || !name) return NULL;
    frontend_config_source *game=frontend_config_store_source(manager,console);
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (!game && !client) return NULL;
    qa_cvars *engine=qa_application_cvars(application);
    qa_application_startup_source source;
    if (!engine) {
        bool held=game?frontend_config_source_tuple(game,&source):frontend_remote_config_tuple(client,&source);
        if (!held || !qa_application_startup_source_engine_cvars(application,&source,&engine,NULL)) return NULL;
    }
    if (manager->shared && !shared_consumed(manager,application) && pending_tuple(manager,application,console,&source)) {
        frontend_shared_values *values=frontend_shared_settings_values(manager->shared);
        const qa_cvars_edit *edit=frontend_shared_values_prepared(values);
        return engine && frontend_shared_values_registry(values)==engine && qa_cvars_edit_registry(edit)==engine?
            qa_cvars_edit_find(edit,name):NULL;
    }
    return engine?qa_cvars_find(engine,name):NULL;
}
bool frontend_config_store_cvar_edit(frontend_config_store *manager,qa_application *application,
    const qa_console *console,const qa_command_context *command,qa_cvars *registry,qa_cvars_edit **out,qa_error *error)
{
    if (!manager || !application || !console || !command || !registry || !out)
        return fail(error,QA_ERROR_ARGUMENT,"Prepared cvar access requires its actual physical console");
    *out=NULL;
    if (!manager->shared) return true;
    if (shared_consumed(manager,application)) {
        frontend_remote_config *client=frontend_config_store_client(manager,console);
        frontend_config_source *game=frontend_config_store_source(manager,console);
        bool admitted=console==qa_application_console(application)?
            qa_application_command_context_active(application,command):
            client?frontend_remote_config_cvar_active(client,command):source_cvar_context(game,command);
        return admitted || fail(error,QA_ERROR_ARGUMENT,"Published values require their actual physical command context");
    }
    frontend_shared_values *values=frontend_shared_settings_values(manager->shared);
    qa_application_client_preparation *client=frontend_shared_settings_client(manager->shared);
    if (client) {
        const qa_application_client_source *actual=qa_application_client_prepare_source(client);
        if (actual && actual->context.console==console) {
            if (registry!=frontend_shared_values_registry(values)) return true;
            qa_cvars *engine=NULL;
            return frontend_shared_values_client_access(values,client,command,&engine,out,error) && engine==registry;
        }
    }
    qa_application_startup_source source;
    if (pending_tuple(manager,application,console,&source))
        return frontend_shared_values_edit(values,&source,command,registry,out,error);
    if (registry!=frontend_shared_values_registry(values)) return true;
    if (application==manager->shared_application && console==manager->root_console &&
        registry==manager->root_cvars && root_current(manager) &&
        same_command(command,&manager->root_command) &&
        qa_application_startup_root_definition_phase(application,manager->shared_candidate) &&
        frontend_config_store_shared(manager,application,manager->shared_candidate)) {
        qa_cvars *actual=NULL;
        return frontend_shared_values_root_definition_access(values,console,command,&actual,out,error) && actual==registry;
    }
    if (application==manager->shared_application && console==manager->root_console &&
        registry==manager->root_cvars && root_current(manager) &&
        same_command(command,&manager->root_command) &&
        qa_application_startup_bootstrap_images_ready(application) && qa_console_idle(console) &&
        frontend_config_store_shared(manager,application,manager->shared_candidate)) {
        qa_cvars *actual=NULL;
        return frontend_shared_values_root_access(values,console,command,&actual,out,error) && actual==registry;
    }
    qa_cvars *actual=NULL;
    return application==manager->shared_application &&
        frontend_shared_values_release_access(values,manager->frontend->input_settings,console,command,&actual,out,error) &&
        actual==registry;
}
bool frontend_config_store_config_filtered(frontend_config_store *manager,qa_application *application,
    const qa_console *console,const qa_command_context *command,qa_cvars *registry,
    qa_cvar_config_filter filter,void *context,qa_buffer *out,qa_error *error)
{
    qa_cvars_edit *edit=NULL;
    if (!frontend_config_store_cvar_edit(manager,application,console,command,registry,&edit,error)) return false;
    return edit?qa_cvars_edit_config_filtered(edit,filter,context,out,error):
        qa_cvars_config_filtered(registry,filter,context,out,error);
}
bool frontend_config_store_stage_input(frontend_config_store *manager,const qa_console *console,
    const qa_command_invocation *command,bool *staged,qa_error *error)
{
    if (!manager || !console || !command || !staged)
        return fail(error,QA_ERROR_ARGUMENT,"Input restart staging requires its actual invocation");
    *staged=false;
    if (!manager->shared) return true;
    if (shared_consumed(manager,manager->shared_application)) return true;
    frontend_shared_values *values=frontend_shared_settings_values(manager->shared);
    qa_application_client_preparation *client=frontend_shared_settings_client(manager->shared);
    if (client) {
        const qa_application_client_source *actual=qa_application_client_prepare_source(client);
        if (actual && actual->context.console==console) {
            bool ok=frontend_shared_values_client_input_restart(values,client,&command->context,error);
            if (ok) *staged=true;
            return ok;
        }
    }
    qa_application_startup_source source;
    bool ok=pending_tuple(manager,manager->shared_application,console,&source)?
        frontend_shared_values_input_restart(values,&source,&command->context,error):
        frontend_shared_values_release_input_restart(values,manager->frontend->input_settings,console,&command->context,error);
    if (ok) *staged=true;
    return ok;
}
bool frontend_config_store_apply_archive(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_cvars *registry,
    const qa_cvar_archive *archive,bool shared,qa_error *error)
{
    if (!manager || !registry || !archive || (archive->count && !archive->entries) ||
        !frontend_config_store_source_pending(manager,application,candidate,source) ||
        manager->shared_application!=application || manager->shared_candidate!=candidate || !manager->shared)
        return fail(error,QA_ERROR_ARGUMENT,"Archive application lost its exact pending shared and private owners");
    frontend_shared_values *values=frontend_shared_settings_values(manager->shared);
    const qa_cvars_edit *edit=frontend_shared_values_prepared(values);
    if (!edit) return fail(error,QA_ERROR_ARGUMENT,"Archive application lost its actual canonical edit");
    for (size_t i=0;i<archive->count;++i) {
        const qa_cvar_archive_entry *entry=archive->entries+i;
        if (!entry->name || !entry->value)
            return fail(error,QA_ERROR_FORMAT,"Archive entry lacks its retained scalar bytes");
        qa_cvar_archive one={.entries=(qa_cvar_archive_entry *)entry,.count=1};
        if (qa_cvars_edit_find(edit,entry->name)) {
            if (shared && !frontend_shared_values_archive(values,&one,error)) return false;
        } else if (!qa_cvar_archive_apply(registry,&one,error)) return false;
    }
    return true;
}
bool frontend_config_store_apply_shared_archive(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    bool primary=false;
    if (!manager || !manager->shared_seeded ||
        !frontend_config_store_shared(manager,application,candidate) ||
        !frontend_config_store_source_pending(manager,application,candidate,source) ||
        !startup_source(manager,application,candidate,source,&primary,error))
        return fail(error,QA_ERROR_ARGUMENT,"Shared archive requires its completed snapshot and pending source");
    if (!primary || manager->shared_archived) return true;
    if (!frontend_shared_values_archive(frontend_shared_settings_values(manager->shared),&manager->shared_archive,error)) return false;
    manager->shared_archived=true; return true;
}
frontend_config_source *frontend_config_store_named_source(const frontend_config_store *owner,const char *name)
{
    for (frontend_config_source *source=owner?owner->sources:NULL;source;source=source->next) {
        const qa_launch_instance *selected=instance(source);
        if (selected && name && !strcmp(selected->selection.instance,name) && !source->imported) return source;
    }
    return NULL;
}
bool frontend_config_source_clone_bindings(const frontend_config_source *source,uint32_t logical,
    frontend_authored_bindings **out,qa_error *error)
{
    size_t index=source?seat_index(source,logical):0;
    return source && index<source->seat_count &&
        frontend_authored_bindings_clone(source->seats[index].authored,out,error);
}
qa_settings_store frontend_config_store_input_store(const frontend_config_store *owner)
{
    qa_vfs *files=owner && owner->frontend?owner->frontend->input_config:NULL;
    for (size_t i=0;files && i<qa_vfs_mount_count(files);++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(files,i,&mount) && mount.writable) return (qa_settings_store){files,mount.id};
    }
    return (qa_settings_store){0};
}
frontend_config_files *frontend_config_source_files(const frontend_config_source *source) { return source?source->files:NULL; }
frontend_key_profile *frontend_config_source_keys(const frontend_config_source *source) { return source?source->keys:NULL; }
qa_cvars *frontend_config_source_cvars(const frontend_config_source *source) { return source?source->cvars:NULL; }
qa_console *frontend_config_source_console(const frontend_config_source *source) { return source?source->console:NULL; }
bool frontend_config_source_tuple(const frontend_config_source *source,qa_application_startup_source *out)
{
    if (!source || !out || source->imported || !source->configured || !source->released ||
        !instance(source) || !source->console || !source->cvars ||
        qa_console_cvars(source->console)!=source->cvars) return false;
    *out=(qa_application_startup_source){instance(source),source->scope,source->console,source->cvars,
        source->command,source->declaration_owner}; return true;
}
qa_application_console_scope frontend_config_source_scope(const frontend_config_source *source)
{ return source?source->scope:(qa_application_console_scope){0}; }
bool frontend_config_source_primary(const frontend_config_source *source) { return source && source->primary; }
bool frontend_config_source_published(const frontend_config_source *source) { return source && source->published; }
qa_input_seat *frontend_config_source_input(const frontend_config_source *source,uint32_t seat)
{
    if (!source) return NULL;
    size_t index=seat_index(source,seat); if (index>=source->seat_count) return NULL;
    qa_frontend *f=source->manager->frontend;
    return source->published?f->seats && index<f->options.seats?f->seats[index].input:NULL:source->seats[index].input;
}
qa_cvars *frontend_config_source_seat_cvars(const frontend_config_source *source,uint32_t seat)
{ size_t index=source?seat_index(source,seat):0; return source && index<source->seat_count?source->seats[index].cvars:NULL; }
qa_cvars *frontend_config_source_mouse_cvars(const frontend_config_source *source,uint32_t seat)
{ size_t index=source?seat_index(source,seat):0; return source && index<source->seat_count?source->seats[index].mouse:NULL; }
bool frontend_config_source_registry_retain(frontend_config_source *source,qa_error *error)
{
    if (!source || source->running || source->registry_references==SIZE_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Client registry context needs its retained actual configuration row");
    ++source->registry_references; return true;
}
bool frontend_config_source_registry_release(frontend_config_source *source,qa_error *error)
{
    if (!source || source->running || !source->registry_references)
        return fail(error,QA_ERROR_ARGUMENT,"Client registry context has no releasable actual configuration row");
    --source->registry_references; return true;
}
static bool registry_context_retain(void *context,qa_error *error)
{ return frontend_config_source_registry_retain(context,error); }
static bool registry_context_release(void *context,qa_error *error)
{ return frontend_config_source_registry_release(context,error); }
bool frontend_config_source_acquire_seat_registry(frontend_config_source *source,uint32_t logical,
    frontend_client_registry **out,qa_error *error)
{
    size_t index=source?seat_index(source,logical):0;
    config_seat *seat=source && index<source->seat_count?source->seats+index:NULL;
    const qa_launch_instance *selected=source?instance(source):NULL;
    if (!seat || !out || *out || !selected || source->running || source->imported)
        return fail(error,QA_ERROR_ARGUMENT,"Client registry acquisition needs its actual configured source seat");
    if (!seat->registry) {
        bool restored=source->manager->restoring && seat->cvars_transferred && !seat->cvars && seat->registry_instance;
        qa_cvars *owned=restored?registry(source,source->command.dialect,error):seat->cvars;
        if (restored && !owned) return false;
        if (!owned || (seat->cvars_transferred && !restored))
            return fail(error,QA_ERROR_ARGUMENT,"Client registry has no unclaimed actual prepared heap");
        frontend_client_registry_context callback={source,registry_context_retain,registry_context_release};
        if (!frontend_client_registry_create(source->manager->frontend,selected,logical,&owned,
            &callback,&seat->registry,error)) {
            if (restored) qa_cvars_destroy(owned);
            return false;
        }
        seat->cvars=frontend_client_registry_cvars(seat->registry); seat->cvars_transferred=true;
        seat->registry_bound=!restored;
    }
    if (!frontend_client_registry_matches(seat->registry,selected,logical) ||
        frontend_client_registry_cvars(seat->registry)!=seat->cvars)
        return fail(error,QA_ERROR_ARGUMENT,"Client registry acquisition left its canonical source owner");
    return frontend_client_registry_retain(seat->registry,out,error);
}
qa_cvars *frontend_config_store_primary_mouse_cvars(const frontend_config_store *manager,uint32_t logical,
    qa_movement_kind *movement)
{
    qa_application *application=manager && manager->frontend?manager->frontend->application:NULL;
    const qa_launch_snapshot *published=application?qa_application_launch(application):NULL;
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(published),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(published,entities->instance):NULL;
    if (!selected) return NULL;
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        const qa_launch_instance *retained=instance(source);
        if (!source->published || !source->primary || source->imported || source->application!=application ||
            !retained || retained->state!=selected->state ||
            strcmp(retained->selection.instance,selected->selection.instance)) continue;
        size_t index=seat_index(source,logical);
        if (index>=source->seat_count) return NULL;
        if (movement) *movement=(qa_movement_kind)source->seats[index].movement_dialect;
        return source->seats[index].mouse;
    }
    return NULL;
}
static frontend_config_source *published_primary(const frontend_config_store *manager,qa_application *application)
{
    const qa_launch_snapshot *published=qa_application_launch(application);
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(published),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(published,entities->instance):NULL;
    if (!selected) return NULL;
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        const qa_launch_instance *retained=instance(source);
        if (source->application==application && source->published && source->primary && !source->imported &&
            retained && retained->storage==selected->storage && retained->state==selected->state) return source;
    }
    return NULL;
}
bool frontend_config_store_server_invocation_read(frontend_config_store *manager,
    const qa_command_invocation *call,qa_application_startup_source *out,qa_error *error)
{
    if (!manager || !call || !out || !call->console ||
        !qa_console_invocation_current(call->console,call))
        return fail(error,QA_ERROR_ARGUMENT,"Server operator requires its actual entered Source invocation");
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        if (!source->primary || source->console!=call->console || !source_context(source,&call->context)) continue;
        const qa_launch_instance *selected=instance(source);
        if (!selected || !source->cvars || qa_console_cvars(source->console)!=source->cvars)
            return fail(error,QA_ERROR_ARGUMENT,"Server operator lost its retained physical Source");
        *out=(qa_application_startup_source){.descriptor=selected,.scope=source->scope,
            .console=source->console,.cvars=source->cvars,.command=call->context,
            .declaration_owner=source->declaration_owner}; return true;
    }
    return fail(error,QA_ERROR_ARGUMENT,"Server operator has no actual primary Source namespace");
}
bool frontend_config_store_server_invocation_application(frontend_config_store *manager,
    const qa_command_invocation *call,qa_application **out,qa_error *error)
{
    qa_application_startup_source source;
    if (!out || !frontend_config_store_server_invocation_read(manager,call,&source,error)) return false;
    frontend_config_source *physical=frontend_config_store_source(manager,source.console);
    if (!physical) return fail(error,QA_ERROR_ARGUMENT,"Server operator lost its actual application owner");
    *out=physical->application; return true;
}
bool frontend_config_store_server_write_root(frontend_config_store *manager,
    const qa_command_invocation *call, qa_settings_store *store, qa_fs_root **root, qa_error *error)
{
    qa_application_startup_source source;
    if (!store || !root || !frontend_config_store_server_invocation_read(manager,call,&source,error)) return false;
    if (call->context.origin==QA_COMMAND_REMOTE)
        return fail(error,QA_ERROR_ARGUMENT,"Remote client has no local Source file authority");
    frontend_config_source *physical=frontend_config_store_source(manager,source.console);
    *store=frontend_config_files_store(physical->files,false);
    *root=frontend_config_files_root(physical->files,false);
    return (*root && qa_vfs_mount_root(store->vfs,store->mount)==*root) ||
        fail(error,QA_ERROR_ARGUMENT,"Source command lost its retained product write root");
}
static frontend_config_source *qw_log_source(frontend_config_store *manager, qa_application *app,
    const qa_application_startup_source *physical)
{
    if (!manager || manager->restoring || !physical || !physical->descriptor ||
        physical->command.dialect!=QA_CONSOLE_QW) return NULL;
    frontend_config_source *source=frontend_config_store_source(manager,physical->console);
    const qa_launch_instance *selected=source?instance(source):NULL;
    return source && source->primary && !source->imported && source->application==app && selected &&
        selected->storage==physical->descriptor->storage && selected->state==physical->descriptor->state &&
        source->scope.kind==physical->scope.kind && source->scope.provider==physical->scope.provider &&
        source->declaration_owner==physical->declaration_owner && source->cvars==physical->cvars &&
        source->command.owner==physical->command.owner?source:NULL;
}
static void qw_log_write(void *context,qa_application *app,
    const qa_application_startup_source *source,const char *record)
{
    frontend_config_store *manager=context;
    if (qw_log_source(manager,app,source)) frontend_qw_logfile_write(manager->qw_logfile,record);
}
static bool qw_log_enabled(void *context,qa_application *app,
    const qa_application_startup_source *source,bool *enabled,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!enabled || !qw_log_source(manager,app,source))
        return fail(error,QA_ERROR_ARGUMENT,"QuakeWorld frag logging lost its physical Source namespace");
    *enabled=frontend_qw_logfile_enabled(manager->qw_logfile); return true;
}
static bool source_common_command(void *context,qa_application *app,
    const qa_application_startup_source *source,const qa_command_invocation *call,
    bool *handled,qa_error *error)
{
    frontend_config_store *manager=context;
    qa_application_startup_source actual;
    if (!manager || !source || !call || !handled || call->console!=source->console ||
        !qa_console_invocation_current(call->console,call))
        return fail(error,QA_ERROR_ARGUMENT,"Common command requires its actual Source invocation");
    *handled=false;
    frontend_config_source *invoked=frontend_config_store_source(manager,call->console);
    if (!invoked || !invoked->primary || call->context.origin==QA_COMMAND_REMOTE) return true;
    if (!frontend_config_store_server_invocation_read(manager,call,&actual,error)) return false;
    frontend_config_source *physical=frontend_config_store_source(manager,actual.console);
    if (!physical || physical->application!=app || source->console!=actual.console ||
        source->cvars!=actual.cvars || source->scope.kind!=actual.scope.kind ||
        source->scope.provider!=actual.scope.provider || source->declaration_owner!=actual.declaration_owner ||
        !source->descriptor || source->descriptor->storage!=actual.descriptor->storage ||
        source->descriptor->state!=actual.descriptor->state)
        return fail(error,QA_ERROR_ARGUMENT,"Common command lost its actual primary Source");
    return frontend_commands_source(manager->frontend,&actual,call,handled,error);
}
static frontend_config_source *source_files_owner(frontend_config_store *manager,qa_application *app,
    const qa_application_startup_source *source)
{
    frontend_config_source *physical=source?frontend_config_store_source(manager,source->console):NULL;
    const qa_launch_instance *descriptor=physical?instance(physical):NULL;
    return physical && physical->application==app && source->descriptor && descriptor &&
        descriptor->storage==source->descriptor->storage && descriptor->state==source->descriptor->state &&
        physical->cvars==source->cvars && physical->scope.kind==source->scope.kind &&
        physical->scope.provider==source->scope.provider && physical->declaration_owner==source->declaration_owner &&
        physical->command.owner==source->command.owner && source->command.dialect==QA_CONSOLE_QW?physical:NULL;
}
static bool source_files(void *context,qa_application *app,const qa_application_startup_source *source,
    qa_launch_source_files *out,const char **directory,qa_error *error)
{
    frontend_config_source *owner=source_files_owner(context,app,source);
    if (!owner || !out || !directory || !frontend_config_files_source_read(owner->files,out,error))
        return fail(error,QA_ERROR_ARGUMENT,"Source filesystem lost its actual retained configuration tuple");
    *directory=frontend_config_files_source_directory(owner->files);
    return *directory!=NULL || fail(error,QA_ERROR_ARGUMENT,"Source filesystem has no reached native write directory");
}
static bool source_gamedir(void *context,qa_application *app,const qa_application_startup_source *source,
    const qa_command_invocation *call,const char *directory,bool *changed,qa_error *error)
{
    frontend_config_source *owner=source_files_owner(context,app,source);
    if (!owner || !call || call->console!=source->console || call->context.origin==QA_COMMAND_REMOTE ||
        !qa_console_invocation_current(call->console,call))
        return fail(error,QA_ERROR_ARGUMENT,"Source gamedir lost its current local operator invocation");
    return frontend_config_files_source_gamedir(owner->files,directory,changed,error);
}
static bool fraglog_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_config_source *source=context; qa_settings_store store={0}; qa_fs_root *root=NULL;
    if (source->console!=call->console ||
        !frontend_config_store_server_write_root(source->manager,call,&store,&root,error)) return false;
    return frontend_qw_logfile_toggle(store,call->console,&call->context,&source->manager->qw_logfile,error);
}
bool frontend_config_store_primary_server_read(frontend_config_store *manager,
    qa_application_startup_source *out,bool *present,qa_error *error)
{
    if (!manager || !out || !present) return fail(error,QA_ERROR_ARGUMENT,"Server console read requires its actual configuration owner");
    *out=(qa_application_startup_source){0}; *present=false;
    frontend_config_source *source=published_primary(manager,manager->frontend->application);
    if (!source && manager->parked_live && !qa_application_launch(manager->frontend->application))
        source=manager->parked;
    if (!source) return true;
    qa_command_context command;
    if (!source->configured || !source->console || !source->cvars ||
        qa_console_cvars(source->console)!=source->cvars || !current_command(source,&command,error)) return false;
    *out=(qa_application_startup_source){.descriptor=instance(source),.scope=source->scope,
        .console=source->console,.cvars=source->cvars,.command=command,.declaration_owner=source->declaration_owner};
    *present=true; return true;
}
bool frontend_config_store_admin_dispatch(frontend_config_store *manager,const qa_command_invocation *call,
    size_t skip,bool *handled,qa_error *error)
{
    qa_application_startup_source source;
    if (!frontend_config_store_server_invocation_read(manager,call,&source,error)) return false;
    if (!manager->admin)
        return fail(error,QA_ERROR_ARGUMENT,"Early Source operator lost its retained administration owner");
    frontend_config_source *physical=frontend_config_store_source(manager,call->console);
    if (!physical || !frontend_source_admin_bind(manager->admin,physical->application,
        source.console,source.cvars,&call->context,error)) return false;
    return frontend_source_admin_dispatch(manager->admin,call,skip,handled,error);
}
bool frontend_config_store_admin_pending(const frontend_config_store *manager)
{ return manager && manager->admin; }
bool frontend_config_store_admin_adopt(frontend_config_store *manager,qa_error *error)
{
    if (!manager || !manager->frontend->network)
        return fail(error,QA_ERROR_ARGUMENT,"Source administration adoption requires its real Network owner");
    if (!manager->admin) return true;
    qa_application_startup_source source; bool present=false;
    if (!frontend_config_store_primary_server_read(manager,&source,&present,error)) return false;
    if (present && !frontend_source_admin_bind(manager->admin,manager->frontend->application,
        source.console,source.cvars,&source.command,error)) return false;
    if (!frontend_source_admin_adopt(manager->admin,error) ||
        !frontend_source_admin_destroy(manager->admin,error)) return false;
    manager->admin=NULL; return true;
}
bool frontend_config_store_primary_legacy_read(const frontend_config_store *manager,uint32_t logical,
    frontend_config_legacy_view *out,bool *present,qa_error *error)
{
    if (present) *present=false;
    if (out) *out=(frontend_config_legacy_view){0};
    qa_application *application=manager && manager->frontend?manager->frontend->application:NULL;
    if (!application || !out || !present)
        return fail(error,QA_ERROR_ARGUMENT,"Legacy policy requires its actual published configuration manager");
    frontend_config_source *source=published_primary(manager,application);
    if (!source) return true;
    const qa_launch_instance *selected=instance(source);
    const qa_product *product=qa_catalog_product(qa_launch_instance_catalog(selected),selected->selection.product);
    if (!product || !product->builtin || product->program_kind!=QA_PROGRAM_BUILTIN ||
        (product->family!=QA_GAME_Q1 && product->family!=QA_GAME_Q2)) return true;
    size_t ordinal=seat_index(source,logical);
    if (ordinal>=source->seat_count) return true;
    qa_cvars *registry=source->seats[ordinal].cvars;
    qa_command_context command;
    if (source->running || !source->configured || !source->released || !registry ||
        !current_command(source,&command,error) || !frontend_legacy_source_owns(registry,"r_fullbright") ||
        !qa_cvars_observer_idle(registry))
        return fail(error,QA_ERROR_ARGUMENT,"Legacy policy lost its returned physical CLIENT declarations");
    *out=(frontend_config_legacy_view){source,selected,product,registry,logical,command.generation}; *present=true; return true;
}
bool frontend_config_store_primary_legacy_current(const frontend_config_store *manager,
    const frontend_config_legacy_view *view)
{
    frontend_config_legacy_view actual; bool present=false;
    return view && frontend_config_store_primary_legacy_read(manager,view->authored_seat,&actual,&present,NULL) &&
        present && actual.source==view->source && actual.descriptor==view->descriptor &&
        actual.product==view->product && actual.registry==view->registry && actual.command_generation==view->command_generation;
}
bool frontend_config_store_select_bindings(frontend_config_store *manager,uint32_t logical,
    qa_strings *strings,const qa_item_definition *items,size_t count,int32_t controller,qa_error *error)
{
    bool present=false;
    if (manager) {
        if (!frontend_neutral_config_select_bindings(manager->neutral,logical,strings,items,count,
            controller,&present,error)) return false;
        if (present) return true;
    }
    qa_application *application=manager && manager->frontend?manager->frontend->application:NULL;
    frontend_config_source *source=application?published_primary(manager,application):NULL;
    size_t ordinal=source?seat_index(source,logical):0;
    qa_input_seat *input=source?frontend_config_source_input(source,logical):NULL;
    if (!source || ordinal>=source->seat_count || !input || source->running || !source->configured || !source->released)
        return fail(error,QA_ERROR_ARGUMENT,"Selected bindings lost their actual published authored seat");
    return frontend_authored_bindings_select(source->seats[ordinal].authored,input,
        source->seats[ordinal].movement_dialect,strings,items,count,controller<0?0:controller,error);
}
bool frontend_config_store_reset_bindings(frontend_config_store *manager,uint32_t logical,
    int32_t controller,qa_error *error)
{
    bool present=false;
    if (manager) {
        if (!frontend_neutral_config_reset_bindings(manager->neutral,logical,controller,&present,error)) return false;
        if (present) return true;
    }
    if (manager && frontend_network_remote(manager->frontend)) {
        frontend_remote_config_view view;
        if (!frontend_network_client_configuration(manager->frontend,logical,&view,error)) return false;
        frontend_remote_config *client=frontend_config_store_client(manager,view.console);
        return client && frontend_remote_config_current(client,&view) &&
            frontend_remote_config_reset_bindings(client,controller,error);
    }
    qa_application *application=manager && manager->frontend?manager->frontend->application:NULL;
    frontend_config_source *source=application?published_primary(manager,application):NULL;
    size_t ordinal=source?seat_index(source,logical):0;
    qa_input_seat *input=source?frontend_config_source_input(source,logical):NULL;
    if (!source || ordinal>=source->seat_count || !input || source->running || !source->configured || !source->released)
        return fail(error,QA_ERROR_ARGUMENT,"Binding Reset lost its actual published authored seat");
    return frontend_authored_bindings_reset(source->seats[ordinal].authored,input,controller<0?0:controller,error);
}
qa_input_seat *frontend_config_store_candidate_input(const frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,unsigned ordinal)
{
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(candidate,entities->instance):NULL;
    if (!manager || !application || !selected || ordinal>=manager->frontend->options.seats ||
        !choices || ordinal>=choices->seat_count) return NULL;
    if (manager->input_prepared && manager->input_application==application &&
        manager->input_candidate==candidate && ordinal<manager->input_count &&
        manager->input_source && manager->input_source->published && manager->input_source->primary) {
        const qa_launch_instance *held=instance(manager->input_source);
        const config_seat *seat=manager->input_seats+ordinal;
        qa_console_dialect movement;
        if (held && held->storage==selected->storage && held->state==selected->state &&
            seat->logical==choices->seats[ordinal].id && seat->input &&
            seat_movement(candidate,seat->logical,&movement,NULL) && movement==seat->movement_dialect &&
            qa_input_seat_context(seat->input).dialect==movement)
            return seat->input;
    }
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        const qa_launch_instance *retained=instance(source);
        if (source->application!=application || source->published || !source->primary ||
            source->candidate!=candidate || !retained || retained->storage!=selected->storage ||
            retained->state!=selected->state || ordinal>=source->seat_count ||
            source->seats[ordinal].logical!=choices->seats[ordinal].id) continue;
        return source->seats[ordinal].input;
    }
    return NULL;
}
qa_input_seat *frontend_config_store_prepared_input(const frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,unsigned ordinal)
{
    qa_input_seat *input=frontend_config_store_candidate_input(manager,application,candidate,ordinal);
    return input?input:manager?frontend_remote_configs_candidate_input(manager->clients,application,candidate,ordinal):NULL;
}
bool frontend_config_store_input_configuration(const frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,unsigned ordinal,qa_input_seat **out,qa_error *error)
{
    if (out) *out=NULL;
    const qa_frontend *f=manager?manager->frontend:NULL;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    if (!candidate && f && out && application && f->application==application &&
        manager->shared_candidate==NULL && root_current(manager) && !qa_application_launch(application) &&
        (qa_application_startup_root_phase(application,NULL) || qa_application_startup_resource_phase(application,NULL)) &&
        f->seats && ordinal<f->options.seats) {
        const frontend_seat *physical=f->seats+ordinal;
        qa_command_context command=physical->console?qa_seat_console_context_read(physical->console):(qa_command_context){0};
        if (physical->frontend!=f || physical->id!=ordinal || !physical->input || !physical->console ||
            qa_input_seat_ordinal(physical->input)!=ordinal || command.owner || command.actor.registry ||
            !frontend_seat_context_ready((void *)physical,ordinal,&command,error))
            return fail(error,QA_ERROR_ARGUMENT,"Bootstrap input lost its actual source-free physical ENGINE seat");
        *out=physical->input; return true;
    }
    if (!f || !out || !application || f->application!=application || !candidate ||
        qa_application_startup_candidate(application)!=candidate || !f->seats ||
        ordinal>=f->options.seats || !choices || ordinal>=choices->seat_count ||
        !choices->seats[ordinal].local || choices->seats[ordinal].bot)
        return fail(error,QA_ERROR_ARGUMENT,"Input configuration lost its actual candidate physical seat");
    const frontend_seat *physical=f->seats+ordinal;
    if (physical->frontend!=f || physical->id!=ordinal || !physical->input || !physical->console ||
        qa_input_seat_ordinal(physical->input)!=ordinal)
        return fail(error,QA_ERROR_ARGUMENT,"Input configuration lost its installed physical input root");
    qa_input_seat *input=frontend_config_store_prepared_input(manager,application,candidate,ordinal);
    if (input) {
        if (qa_input_seat_ordinal(input)!=ordinal)
            return fail(error,QA_ERROR_ARGUMENT,"Prepared dictionary belongs to another physical seat");
        *out=input; return true;
    }
    const qa_launch_snapshot *published=qa_application_launch(application);
    const qa_launch_choices *old=qa_launch_snapshot_choices(published);
    uint32_t logical=choices->seats[ordinal].id;
    if (old && (ordinal>=old->seat_count || old->seats[ordinal].id!=logical ||
        !old->seats[ordinal].local || old->seats[ordinal].bot))
        return fail(error,QA_ERROR_ARGUMENT,"Unchanged input changed its published authored seat");
    bool unchanged=false;
    if (frontend_network_remote(f)) {
        frontend_remote_config_view view; bool present=false;
        if (!frontend_network_client_previous_configuration_read(f,logical,&view,&present,error)) return false;
        const qa_launch_instance *selected=present && view.receiver?
            qa_launch_snapshot_find(candidate,view.receiver->selection.instance):NULL;
        qa_console_dialect movement;
        unchanged=present && view.ready && view.published && view.physical_seat==ordinal &&
            view.scope.seat==logical && selected && selected->state==view.receiver->state &&
            selected->storage==view.receiver->storage && seat_movement(candidate,logical,&movement,error) &&
            movement==(qa_console_dialect)view.movement;
    } else {
        const qa_launch_binding *binding=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        const qa_launch_instance *selected=binding?qa_launch_snapshot_find(candidate,binding->instance):NULL;
        frontend_config_source *source=published_primary(manager,application);
        const qa_launch_instance *held=source?instance(source):NULL;
        qa_console_dialect movement;
        unchanged=source && held && selected && held->storage==selected->storage &&
            held->state==selected->state && source->configured && source->released &&
            !source->imported && !source->running && ordinal<source->seat_count &&
            source->seats[ordinal].logical==logical && seat_movement(candidate,logical,&movement,error) &&
            movement==source->seats[ordinal].movement_dialect;
        if (!binding && !qa_launch_binding_for(old,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"")) {
            qa_command_context command=qa_seat_console_context_read(physical->console);
            unchanged=command.origin==QA_COMMAND_SEAT && !command.owner && command.seat==logical &&
                (!command.actor.registry || (old && qa_application_command_context_active(application,&command)));
        }
    }
    if (!unchanged)
        return fail(error,QA_ERROR_ARGUMENT,"Changed input source has no actual prepared dictionary");
    *out=physical->input; return true;
}
bool frontend_config_store_view_transition(const frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,frontend_view_transition *out,qa_error *error)
{
    const qa_frontend *f=manager?manager->frontend:NULL;
    bool published=false;
    if (!out || !f || f->application!=application || (!candidate && !root_current(manager)) ||
        !frontend_config_store_shared(manager,application,candidate) ||
        !qa_application_startup_resource_phase(application,candidate) ||
        !frontend_view_settings_parent_is(f->view_settings,f,qa_application_cvars(application)) ||
        !frontend_view_settings_has_published(f->view_settings,&published))
        return fail(error,QA_ERROR_ARGUMENT,"View transition lost its actual candidate and published preference owner");
    *out=published?FRONTEND_VIEW_REPLACEMENT:FRONTEND_VIEW_INITIAL;
    return true;
}
static qa_input_seat *binding_seat(void *context,const qa_command_context *command)
{
    frontend_config_source *source=context;
    if (!source_context(source,command)) return NULL;
    size_t seat=command->origin==QA_COMMAND_SEAT?seat_index(source,command->seat):0;
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
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client?!frontend_remote_config_cvar_active(client,command):!source_cvar_context(source,command)) return NULL;
    qa_application *application=manager->frontend->application;
    qa_cvars *engine=qa_application_cvars(application);
    qa_cvars *private=NULL;
    if (client) private=frontend_remote_config_cvar_owner(client,command,name);
    else if (command->origin!=QA_COMMAND_SERVER && source->seat_count) {
        size_t seat=command->origin==QA_COMMAND_SEAT?seat_index(source,command->seat):0;
        if (seat<source->seat_count) private=source->seats[seat].cvars;
    }
    if (name && frontend_legacy_source_owns(private,name)) return private;
    qa_application_startup_source tuple;
    if (name && manager->shared && !shared_consumed(manager,application) && pending_tuple(manager,application,console,&tuple)) {
        qa_cvars *owner=NULL;
        if (!frontend_shared_values_resolve(frontend_shared_settings_values(manager->shared),
            &tuple,command,name,&owner,NULL)) return NULL;
        if (owner) return owner;
    } else if (name && engine && qa_cvars_find(engine,name)) return engine;
    if (client) return frontend_remote_config_cvar_owner(client,command,name);
    if (!source_cvar_context(source,command) || !name) return NULL;
    if (command->dialect==QA_CONSOLE_Q3 && equal(name,"sv_cheats")) return NULL;
    if (qa_cvars_find(source->cvars,name)) return source->cvars;
    size_t ordinal=command->origin==QA_COMMAND_SEAT?seat_index(source,command->seat):0;
    if (command->origin==QA_COMMAND_SEAT && ordinal>=source->seat_count) return NULL;
    config_seat *seat=command->origin==QA_COMMAND_SERVER || !source->seat_count?NULL:
        source->seats+ordinal;
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
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client?!frontend_remote_config_cvar_active(client,command):!source_cvar_context(source,command)) return NULL;
    qa_application *application=manager->frontend->application;
    qa_cvars *engine=qa_application_cvars(application);
    if (engine) {
        qa_application_startup_source tuple;
        if (manager->shared && !shared_consumed(manager,application) && pending_tuple(manager,application,console,&tuple)) {
            qa_cvars_edit *edit=NULL;
            if (!frontend_shared_values_edit(frontend_shared_settings_values(manager->shared),
                &tuple,command,engine,&edit,NULL) || !edit) return NULL;
        }
        if (!ordinal) return engine;
        --ordinal;
    }
    if (client) return frontend_remote_config_visible(client,command,ordinal);
    if (!source_cvar_context(source,command)) return NULL;
    qa_cvars *rows[5]={0}; size_t count=0;
    size_t seat=command->origin==QA_COMMAND_SEAT?seat_index(source,command->seat):0;
    if (command->origin!=QA_COMMAND_SERVER && seat<source->seat_count) rows[count++]=source->seats[seat].mouse;
    rows[count++]=source->cvars;
    if (command->origin==QA_COMMAND_SEAT && seat<source->seat_count) rows[count++]=source->seats[seat].cvars;
    if (source->movement) rows[count++]=source->movement;
    if (source->fallback && source->fallback!=source->movement) rows[count++]=source->fallback;
    return ordinal<count?rows[ordinal]:NULL;
}
static frontend_config_source *namespace_game(const frontend_config_store *manager,qa_application *application,
    const qa_application_startup_source *authority,qa_error *error)
{
    frontend_config_source *source=authority?frontend_config_store_source(manager,authority->console):NULL;
    const qa_launch_instance *held=source?instance(source):NULL;
    if (!source || !authority->descriptor || source->application!=application || source->imported ||
        !source->configured || !source->released ||
        !held || held->storage!=authority->descriptor->storage || !game_scope(authority->scope) ||
        !same_scope(source->scope,authority->scope) || source->cvars!=authority->cvars ||
        qa_console_cvars(source->console)!=source->cvars) {
        fail(error,QA_ERROR_ARGUMENT,"Host namespaces leave their actual GAME configuration"); return NULL;
    }
    return source;
}
static bool registry_inventory(const frontend_config_store *manager,qa_application *application,
    const qa_application_startup_source *authority,const qa_application_startup_source *parent_game,
    const frontend_config_host_cvars *entry,qa_cvars *rows[8],qa_error *error)
{
    if (!manager || !application || !authority || !authority->descriptor || !authority->descriptor->storage ||
        !authority->scope.provider || !authority->console || !authority->cvars ||
        qa_console_cvars(authority->console)!=authority->cvars)
        return fail(error,QA_ERROR_ARGUMENT,"Host namespace inventory needs its physical constructor tuple");
    rows[QA_Q3_HOST_CVAR_ENGINE-1]=qa_application_cvars(application);
    frontend_config_source *game=NULL;
    if (game_scope(authority->scope)) {
        if (parent_game) return fail(error,QA_ERROR_ARGUMENT,"GAME namespace inventory cannot acquire another parent");
        game=namespace_game(manager,application,authority,error);
        if (!game) return false;
    } else if (client_scope(authority->scope)) {
        qa_application_startup_source actual; qa_error ordinary={0};
        bool current=qa_application_q3_client_configuration_read(application,authority->scope.provider,authority->scope.seat,&actual,&ordinary) &&
            actual.descriptor && actual.descriptor->storage==authority->descriptor->storage &&
            same_scope(actual.scope,authority->scope) && actual.console==authority->console && actual.cvars==authority->cvars;
        if (!current && !qa_application_q3_client_configuration_entered(application,authority)) {
            const qa_q3_host *host=NULL; qa_cvars *registry=NULL;
            if (!entry || !entry->entry_read || entry->application!=application || authority!=&entry->source ||
                !entry->entry_read(entry->entry_context,application,authority,&host,error) || !host ||
                qa_q3_host_console(host,&registry,NULL)!=authority->console || registry!=authority->cvars)
                return fail(error,QA_ERROR_ARGUMENT,"Host namespaces differ from the retained physical CLIENT slot");
        }
        frontend_remote_config *client=frontend_config_store_client(manager,authority->console);
        if (client) {
            qa_console *hosted=NULL;
            if (!frontend_remote_config_registries(client,application,authority,rows,&hosted,error)) return false;
            game=hosted?frontend_config_store_source(manager,hosted):NULL;
            if (hosted && (!game || game->application!=application || game->imported || !game->configured ||
                !game->released || !instance(game) ||
                !game_scope(game->scope) || qa_console_cvars(game->console)!=game->cvars))
                return fail(error,QA_ERROR_ARGUMENT,"Host namespaces lost their retained hosted GAME parent");
            if (parent_game && namespace_game(manager,application,parent_game,error)!=game)
                return fail(error,QA_ERROR_ARGUMENT,"Host namespaces name another actual GAME parent");
        } else {
            game=namespace_game(manager,application,parent_game,error);
            size_t seat=game?seat_index(game,authority->scope.seat):0;
            if (!game || seat>=game->seat_count || game->seats[seat].cvars!=authority->cvars ||
                !game->seats[seat].registry || frontend_client_registry_cvars(game->seats[seat].registry)!=authority->cvars)
                return fail(error,QA_ERROR_ARGUMENT,"Supplemental CLIENT namespaces lack their real GAME-seat heap");
            rows[QA_Q3_HOST_CVAR_CLIENT-1]=authority->cvars;
            rows[QA_Q3_HOST_CVAR_MOUSE-1]=game->seats[seat].mouse;
            rows[QA_Q3_HOST_CVAR_SELECTED_VIEW-1]=game->seats[seat].mouse;
        }
    } else return fail(error,QA_ERROR_ARGUMENT,"Host namespaces have no genuine GAME or CLIENT scope");
    if (game) {
        rows[QA_Q3_HOST_CVAR_GAME-1]=game->cvars;
        rows[QA_Q3_HOST_CVAR_MOVEMENT-1]=game->movement;
        rows[QA_Q3_HOST_CVAR_FALLBACK-1]=game->fallback;
    }
    if (!rows[QA_Q3_HOST_CVAR_ENGINE-1] &&
        !qa_application_startup_source_engine_cvars(application,authority,&rows[QA_Q3_HOST_CVAR_ENGINE-1],error)) return false;
    return rows[QA_Q3_HOST_CVAR_ENGINE-1]!=NULL || fail(error,QA_ERROR_ARGUMENT,"Host namespaces lost canonical ENGINE");
}
bool frontend_config_store_registry_reference(const frontend_config_store *manager,qa_application *application,
    const qa_application_startup_source *source,const qa_application_startup_source *parent_game,
    const qa_cvars *registry,qa_q3_host_cvar_namespace *out,qa_error *error)
{
    qa_cvars *rows[8]={0};
    if (!registry || !out) return fail(error,QA_ERROR_ARGUMENT,"Host namespace reference needs its actual registry and output");
    if (!registry_inventory(manager,application,source,parent_game,NULL,rows,error)) return false;
    for (size_t i=0;i<8;++i) if (rows[i]==registry) { *out=(qa_q3_host_cvar_namespace)(i+1); return true; }
    return fail(error,QA_ERROR_ARGUMENT,"Registry is absent from this retained host namespace inventory");
}
bool frontend_config_store_registry_resolve(const frontend_config_store *manager,qa_application *application,
    const qa_application_startup_source *source,const qa_application_startup_source *parent_game,
    qa_q3_host_cvar_namespace reference,qa_cvars **out,qa_error *error)
{
    qa_cvars *rows[8]={0};
    if (!out || reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW)
        return fail(error,QA_ERROR_ARGUMENT,"Host namespace role leaves its retained inventory");
    if (!registry_inventory(manager,application,source,parent_game,NULL,rows,error)) return false;
    if (!rows[reference-1]) return fail(error,QA_ERROR_ARGUMENT,"Host namespace role has no retained physical registry");
    *out=rows[reference-1]; return true;
}
static bool host_registry_reference(void *context,const qa_cvars *registry,qa_q3_host_cvar_namespace *out,qa_error *error)
{
    frontend_config_host_cvars *owner=context;
    qa_cvars *rows[8]={0};
    if (!owner || !registry || !out || !registry_inventory(owner->manager,owner->application,&owner->source,
        owner->has_parent?&owner->parent_game:NULL,owner,rows,error)) return false;
    for (size_t i=0;i<8;++i) if (rows[i]==registry) { *out=(qa_q3_host_cvar_namespace)(i+1); return true; }
    return fail(error,QA_ERROR_ARGUMENT,"Registry is absent from this retained host namespace inventory");
}
static bool host_registry_resolve(void *context,qa_q3_host_cvar_namespace reference,qa_cvars **out,qa_error *error)
{
    frontend_config_host_cvars *owner=context;
    qa_cvars *rows[8]={0};
    if (!owner || !out || reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW ||
        !registry_inventory(owner->manager,owner->application,&owner->source,
            owner->has_parent?&owner->parent_game:NULL,owner,rows,error)) return false;
    if (!rows[reference-1]) return fail(error,QA_ERROR_ARGUMENT,"Host namespace role has no retained physical registry");
    *out=rows[reference-1]; return true;
}
bool frontend_config_host_cvars_prepare(frontend_config_host_cvars *owner,const frontend_config_store *manager,
    qa_application *application,const qa_application_startup_source *source,const qa_application_startup_source *parent_game,
    qa_q3_host_cvar_services *out,qa_error *error)
{
    qa_cvars *engine=NULL;
    if (!owner || owner->manager || !out)
        return fail(error,QA_ERROR_ARGUMENT,"Host namespace callbacks need an empty retained factory context");
    if (!frontend_config_store_registry_resolve(manager,application,source,parent_game,QA_Q3_HOST_CVAR_ENGINE,&engine,error)) return false;
    *owner=(frontend_config_host_cvars){.manager=manager,.application=application,.source=*source,
        .parent_game=parent_game?*parent_game:(qa_application_startup_source){0},.has_parent=parent_game!=NULL};
    *out=(qa_q3_host_cvar_services){owner,host_registry_reference,host_registry_resolve}; return true;
}
bool frontend_config_host_cvars_set_entry(frontend_config_host_cvars *owner,void *context,
    frontend_config_host_entry_read read,qa_error *error)
{
    if (!owner || !owner->manager || !owner->application || !client_scope(owner->source.scope) ||
        owner->entry_read || !context || !read)
        return fail(error,QA_ERROR_ARGUMENT,"Acquired host entry needs its immutable retained CLIENT lease");
    owner->entry_context=context; owner->entry_read=read; return true;
}
static bool same_command(const qa_command_context *a,const qa_command_context *b)
{
    return a && b && a->session==b->session && a->owner==b->owner && a->client==b->client && a->seat==b->seat &&
        a->dialect==b->dialect && a->origin==b->origin && a->direct==b->direct && a->console_text==b->console_text &&
        a->script==b->script && a->registry==b->registry && a->generation==b->generation && qa_actor_id_equal(a->actor,b->actor);
}
static bool host_constructor(const qa_application_startup_source *source,const qa_q3_host *host,
    const qa_console *console,const qa_command_context *command)
{
    qa_cvars *registry=NULL; qa_command_context actual;
    return source && host && console==source->console &&
        qa_q3_host_console(host,&registry,&actual)==console && registry==source->cvars && same_command(command,&actual);
}
bool frontend_config_host_cvar_entered(void *context,const qa_q3_host *host,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    frontend_config_host_cvars *owner=context;
    if (!owner || !host_constructor(&owner->source,host,console,command))
        return fail(error,QA_ERROR_ARGUMENT,"Named cvar entry leaves its actual host constructor");
    if (qa_application_q3_configuration_host_entered(owner->application,&owner->source,host)) return true;
    const qa_q3_host *actual=NULL;
    return owner->entry_read && owner->entry_read(owner->entry_context,owner->application,&owner->source,&actual,error) &&
        actual==host;
}
bool frontend_config_host_bindings(void *context,const qa_input_seat *physical,qa_input_seat **out,qa_error *error)
{
    frontend_config_host_cvars *owner=context;
    qa_cvars *rows[8]={0};
    if (!owner || !physical || !out || !client_scope(owner->source.scope) ||
        !registry_inventory(owner->manager,owner->application,&owner->source,
            owner->has_parent?&owner->parent_game:NULL,owner,rows,error))
        return fail(error,QA_ERROR_ARGUMENT,"Host bindings need their actual retained CLIENT namespace");
    frontend_remote_config *client=frontend_config_store_client(owner->manager,owner->source.console);
    if (client) return frontend_remote_config_bindings(client,owner->application,&owner->source,physical,out,error);
    frontend_config_source *game=namespace_game(owner->manager,owner->application,&owner->parent_game,error);
    size_t ordinal=game?seat_index(game,owner->source.scope.seat):0;
    qa_frontend *f=owner->manager->frontend;
    if (!game || ordinal>=game->seat_count || !f->seats || ordinal>=f->options.seats ||
        f->seats[ordinal].input!=physical || qa_input_seat_ordinal(physical)!=ordinal)
        return fail(error,QA_ERROR_ARGUMENT,"Supplemental CLIENT bindings lost their stable physical seat");
    qa_input_seat *input=frontend_config_source_input(game,owner->source.scope.seat);
    if (!input || qa_input_seat_ordinal(input)!=ordinal)
        return fail(error,QA_ERROR_ARGUMENT,"Supplemental CLIENT bindings lack their genuine dictionary owner");
    *out=input; return true;
}
static bool game_registry_reference(void *context,const qa_cvars *registry,qa_q3_host_cvar_namespace *out,qa_error *error)
{
    frontend_config_source *source=context; qa_application_startup_source tuple;
    if (!frontend_config_source_tuple(source,&tuple)) return fail(error,QA_ERROR_ARGUMENT,"GAME namespace callback lost its retained owner");
    return frontend_config_store_registry_reference(source->manager,source->application,&tuple,NULL,registry,out,error);
}
bool frontend_config_source_cvar_entered(void *context,const qa_q3_host *host,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    frontend_config_source *source=context; qa_application_startup_source tuple,entered;
    if (!frontend_config_source_tuple(source,&tuple) || !host_constructor(&tuple,host,console,command))
        return fail(error,QA_ERROR_ARGUMENT,"Named GAME cvar entry lost its actual host constructor");
    if (!qa_application_q3_game_configuration_entered_read(source->application,host,&entered,error)) return false;
    return (entered.descriptor && entered.descriptor->storage==tuple.descriptor->storage &&
        entered.descriptor->content==tuple.descriptor->content && entered.descriptor->roles==tuple.descriptor->roles &&
        qa_sha256_equal(&entered.descriptor->identity,&tuple.descriptor->identity) &&
        same_scope(entered.scope,tuple.scope) && entered.console==tuple.console && entered.cvars==tuple.cvars &&
        same_command(command,&entered.command)) ||
        fail(error,QA_ERROR_ARGUMENT,"Named GAME cvar entry differs from its retained physical configuration");
}
static bool game_registry_resolve(void *context,qa_q3_host_cvar_namespace reference,qa_cvars **out,qa_error *error)
{
    frontend_config_source *source=context; qa_application_startup_source tuple;
    if (!frontend_config_source_tuple(source,&tuple)) return fail(error,QA_ERROR_ARGUMENT,"GAME namespace callback lost its retained owner");
    return frontend_config_store_registry_resolve(source->manager,source->application,&tuple,NULL,reference,out,error);
}
bool frontend_config_source_host_cvars(frontend_config_source *source,qa_q3_host_cvar_services *out,qa_error *error)
{
    qa_application_startup_source tuple; qa_cvars *engine=NULL;
    if (!out || !frontend_config_source_tuple(source,&tuple) ||
        !frontend_config_store_registry_resolve(source->manager,source->application,&tuple,NULL,QA_Q3_HOST_CVAR_ENGINE,&engine,error))
        return fail(error,QA_ERROR_ARGUMENT,"GAME host namespace callbacks lack their retained configuration row");
    *out=(qa_q3_host_cvar_services){source,game_registry_reference,game_registry_resolve}; return true;
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
    config_seat *seat=source->seats+source->seat_index;
    return frontend_authored_bindings_ready(seat->authored) ||
        frontend_authored_bindings_defaults(seat->authored,seat->input,seat->movement_dialect,error);
}
static bool selected_defaults(frontend_config_source *source,config_seat *seat,bool seed,qa_error *error)
{
    qa_strings *strings=qa_session_strings(qa_application_session(source->application));
    frontend_config_weapon_catalog catalog;
    if (!frontend_config_weapon_defaults(source->application,source->candidate,
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=seat->logical},strings,&catalog,error)) return false;
    return seed?frontend_authored_bindings_seed(seat->authored,seat->movement_dialect,strings,
        catalog.items,catalog.count,error):frontend_authored_bindings_select(seat->authored,seat->input,
        seat->movement_dialect,strings,catalog.items,catalog.count,0,error);
}
static bool archive(void *context,qa_error *error)
{
    frontend_config_source *source=context;
    qa_application_startup_source tuple={instance(source),source->scope,source->console,source->cvars,
        source->command,source->declaration_owner};
    if (!source->seat_index &&
        (!frontend_config_store_apply_archive(source->manager,source->application,source->candidate,&tuple,
            source->cvars,&source->source_archive,true,error) ||
         !frontend_config_store_apply_archive(source->manager,source->application,source->candidate,&tuple,
            source->movement,&source->movement_archive,true,error) ||
         !frontend_config_store_apply_archive(source->manager,source->application,source->candidate,&tuple,
            source->fallback,&source->fallback_archive,true,error))) return false;
    if (!source->seat_index && !frontend_config_store_apply_shared_archive(source->manager,
        source->application,source->candidate,&tuple,error)) return false;
    if (!source->seat_count) return true;
    config_seat *seat=source->seats+source->seat_index;
    if (!frontend_config_store_apply_archive(source->manager,source->application,source->candidate,&tuple,
        seat->cvars,&seat->client_archive,!source->seat_index,error) ||
        !frontend_config_store_apply_archive(source->manager,source->application,source->candidate,&tuple,
        seat->mouse,&seat->mouse_archive,!source->seat_index,error)) return false;
    if (seat->found) {
        if (!qa_input_seat_replace_bindings(seat->input,seat->settings.bindings,seat->settings.binding_count,error) ||
            !qa_input_mouse_settings_write(seat->mouse,&seat->settings.mouse,error)) return false;
        frontend_authored_bindings_profile(seat->authored);
        *qa_input_seat_gamepad_tuning(seat->input)=seat->settings.gamepad;
        if (seat->settings.has_always_run && !qa_cvars_set_flags(seat->mouse,"cl_run",
            seat->settings.always_run?"1":"0",QA_CVAR_ARCHIVE,error)) return false;
    }
    frontend_config_source *previous=published_primary(source->manager,source->application);
    size_t old=previous?seat_index(previous,seat->logical):0;
    if (previous && source!=previous && old<previous->seat_count && old==source->seat_index) {
        qa_input_seat *live=frontend_config_source_input(previous,seat->logical);
        qa_input_command_tuning tuning;
        if (!live || !frontend_authored_bindings_restore_previous(seat->authored,
            previous->seats[old].authored,live,seat->input,error) ||
            !qa_input_settings_read(previous->seats[old].mouse,
                (qa_movement_kind)previous->seats[old].movement_dialect,&tuning,error) ||
            !qa_input_mouse_settings_write(seat->mouse,&tuning.mouse,error)) return false;
        const qa_cvar_view *run=qa_cvars_find(previous->seats[old].mouse,"cl_run");
        if (run && !qa_cvars_set_flags(seat->mouse,"cl_run",run->value,QA_CVAR_ARCHIVE,error)) return false;
    }
    return true;
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
    qa_command_context command=source->command;
    if (source->seat_count) { command.origin=QA_COMMAND_SEAT; command.seat=source->seats[0].logical; }
    return source->seat_index || (qa_application_capture_command_context(source->application,&command,&command,error) &&
        qa_application_startup_replay_variables(source->application,source->console,&command,error));
}
static bool phase_create(frontend_config_source *source,qa_error *error)
{
    qa_command_context command=source->command;
    if (source->seat_count) { command.origin=QA_COMMAND_SEAT; command.seat=source->seats[source->seat_index].logical; }
    if (!qa_application_capture_command_context(source->application,&command,&command,error)) return false;
    bool safe=false;
    if (command.dialect==QA_CONSOLE_Q3) {
        const qa_launch_instance *selected=qa_launch_snapshot_find(source->candidate,instance(source)->selection.instance);
        if (!qa_application_startup_q3_safe_mode(source->application,selected,source->console,&safe,error)) return false;
    }
    frontend_startup_config_options options={.command=command,
        .has_mod=source->has_mod,.safe_mode=safe,
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
    if (input && !qa_input_bindings_config(input,false,out,error)) return false;
    if (source->dedicated_bindings && !frontend_config_bindings_config(source->dedicated_bindings,out,error)) return false;
    config_filter filter={source,command};
    for (size_t i=0;;++i) {
        qa_cvars *registry=frontend_config_store_visible_cvars(source->manager,source->console,command,i);
        if (!registry) break;
        qa_buffer rows={0};
        if (!frontend_config_store_config_filtered(source->manager,source->application,source->console,
            command,registry,routed_archive,&filter,&rows,error)) return false;
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
        size_t seat=command->context.origin==QA_COMMAND_SEAT?seat_index(source,command->context.seat):0;
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
bool frontend_config_store_write_source_text(frontend_config_store *manager,
    const qa_command_invocation *command,const char *path,qa_bytes text,qa_error *error)
{
    if (!manager || !command || !command->console || !path || !*path ||
        (text.size && !text.data) || !qa_console_invocation_current(command->console,command))
        return fail(error,QA_ERROR_ARGUMENT,"Source file write requires its actual entered command invocation");
    for (frontend_config_source *source=manager->sources;source;source=source->next)
        if (source->console==command->console && source_context(source,&command->context))
            return frontend_config_files_write_config_text(source->files,path,&command->context,text,error);
    return fail(error,QA_ERROR_ARGUMENT,"Source file write has no genuine configured source owner");
}
static bool source_destroy(frontend_config_source *source,qa_error *error)
{
    frontend_config_store *manager=source->manager;
    bool parked=manager->parked==source;
    if (!parked && manager->parked_from==source && !parked_seal(manager,source,error)) return false;
    if (parked && manager->parked_program) {
        if (!qa_console_program_abort(manager->parked_program,error)) return false;
        manager->parked_program=NULL;
    }
    if (parked && !qa_console_destroy_ready(source->console))
        return fail(error,QA_ERROR_ARGUMENT,"Stopped ENGINE configuration retains its real command programme borrower");
    if (source->manager->input_source==source)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration source retains its actual staged input candidate");
    size_t contexts=0;
    for (size_t i=0;i<source->seat_count;++i) if (source->seats[i].registry) {
        if (!frontend_client_registry_release_ready(source->seats[i].registry,error)) return false;
        ++contexts;
    }
    if (source->registry_references>contexts)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration source retains other live client registry callback contexts");
    if (source->running || (source->files && !frontend_config_files_idle(source->files)) ||
        !frontend_startup_config_destroy(source->phase,error)) return false;
    source->phase=NULL;
    if (!frontend_config_bindings_destroy(source->dedicated_bindings,error)) return false;
    source->dedicated_bindings=NULL;
    for (size_t i=0;i<source->seat_count;++i) if (source->seats[i].registry) {
        config_seat *seat=source->seats+i;
        if (!frontend_client_registry_release(&seat->registry,error)) {
            seat->cvars=frontend_client_registry_cvars(seat->registry); return false;
        }
        seat->cvars=NULL;
    }
    if (source->registry_references)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration source retains live client registry callback contexts");
    qa_input_console_destroy(source->bindings); source->bindings=NULL;
    if (!frontend_source_admin_unbind(source->manager->admin,source->console,error)) return false;
    frontend_network_source_admin_unbind(source->console,source->command.owner,source->admin_registered);
    source->admin_registered=0;
    if (source->write_registered) qa_console_unregister(source->console,"writeconfig",source->command.owner);
    if (source->dump_registered) qa_console_unregister(source->console,"condump",source->command.owner);
    if (source->frag_registered) qa_console_unregister(source->console,"fraglogfile",source->command.owner);
    source->write_registered=source->dump_registered=source->frag_registered=false;
    if (source->keys) {
        qa_cvars *bound=frontend_key_profile_registry(source->keys);
        if (bound && bound!=source->cvars)
            return fail(error,QA_ERROR_ARGUMENT,"Configuration retirement names another bound key registry");
        if ((bound && !frontend_key_profile_detach(source->keys,bound,error)) ||
            !frontend_key_profile_release(source->keys,error)) return false;
        source->keys=NULL; source->files=NULL;
    } else if (!frontend_config_files_destroy(source->files,error)) return false;
    for (size_t i=0;i<source->seat_count;++i) {
        config_seat *seat=source->seats+i;
        qa_input_seat_destroy(seat->input);
        frontend_authored_bindings_destroy(seat->authored);
        if (!seat->cvars_transferred) qa_cvars_destroy(seat->cvars);
        qa_cvars_destroy(seat->mouse);
        qa_seat_settings_free(&seat->settings); qa_cvar_archive_free(&seat->client_archive); qa_cvar_archive_free(&seat->mouse_archive);
        free(seat->registry_instance);
    }
    if (source->fallback!=source->movement) qa_cvars_destroy(source->fallback);
    qa_cvars_destroy(source->movement);
    qa_cvar_archive_free(&source->source_archive); qa_cvar_archive_free(&source->movement_archive); qa_cvar_archive_free(&source->fallback_archive);
    qa_launch_instance_lease_release(source->metadata); free(source->saved_instance);
    if (manager->parked_from==source) manager->parked_from=NULL;
    if (parked) {
        qa_console_destroy(source->console); qa_cvars_destroy(source->cvars);
        manager->parked=NULL; manager->parked_console=NULL; manager->parked_cvars=NULL;
        manager->parked_from=NULL;
        manager->parked_root=NULL; manager->parked_live=false; manager->parked_sealed=false;
        manager->parked_basis=(qa_application_startup_source){0}; manager->parked_local=false;
    }
    free(source); return true;
}
static bool install_commands(frontend_config_source *source,qa_error *error)
{
    if (source->primary && source->command.dialect==QA_CONSOLE_QW && !source->frag_registered) {
        source->frag_registered=qa_console_register_owned(source->console,"fraglogfile",
            "Toggle the actual QuakeWorld frag file",source->command.owner,source->command.owner,
            true,fraglog_command,source,error);
        if (!source->frag_registered) return false;
    }
    if (source->primary && !source->imported &&
        !qa_server_admin_declarations(source->cvars,source->declaration_owner,error)) return false;
    if (source->primary && !source->imported && !source->manager->frontend->network) {
        frontend_config_store *manager=source->manager;
        if (manager->admin) {
            if (!frontend_source_admin_bind(manager->admin,source->application,source->console,
                source->cvars,&source->command,error)) return false;
        } else if (!frontend_source_admin_create(manager->frontend,source->application,source->console,
            source->cvars,&source->command,&manager->admin,error)) return false;
    }
    if (source->primary && !frontend_network_source_admin_bind(source->manager->frontend,source->console,
        source->command.owner,&source->admin_registered,error)) return false;
    if (source->primary && source->seat_count) {
        qa_input_console_options input={.console=source->console,.owner=source->command.owner,
            .user=source,.seat=binding_seat,.print=print};
        source->bindings=qa_input_console_create(&input,error);
        if (!source->bindings) return false;
    } else if (source->primary) {
        if (!source->dedicated_bindings) source->dedicated_bindings=frontend_config_bindings_create(error);
        frontend_config_binding_commands commands={source->console,source->command.owner,source,binding_context,print};
        if (!source->dedicated_bindings ||
            !frontend_config_bindings_commands(source->dedicated_bindings,&commands,error)) return false;
    }
    source->write_registered=qa_console_register_owned(source->console,"writeconfig",
        "Save the actual source configuration",source->command.owner,source->command.owner,
        true,config_command,source,error);
    if (!source->write_registered) return false;
    source->dump_registered=qa_console_register_owned(source->console,"condump",
        "Dump the actual local console buffer",source->command.owner,source->command.owner,
        true,config_command,source,error);
    return source->dump_registered;
}
static bool registry_carry(frontend_config_source *source,const qa_cvars *previous,
    qa_cvars **out,qa_error *error)
{
    qa_buffer bytes={0}; qa_cvars_restore *ticket=NULL;
    *out=registry(source,qa_cvars_dialect(previous),error);
    bool ok=*out && qa_cvars_save_capture(previous,&bytes,error) &&
        qa_cvars_save_prepare(*out,(qa_bytes){bytes.data,bytes.size},&ticket,error) &&
        qa_cvars_save_commit(ticket,error);
    if (!ok) qa_cvars_save_abort(ticket);
    qa_buffer_free(&bytes);
    /* The new physical registry owns carried scalar records. Role callbacks
     * are rebuilt by its actual factory, never retained from the old host. */
    for (size_t i=0;ok && i<qa_cvars_count(*out);++i) {
        const qa_cvar_view *value=qa_cvars_at(*out,i);
        if (value->owner) ok=qa_cvars_retain_shared(*out,value->name,error);
    }
    return ok;
}
static bool same_text(const char *left,const char *right)
{ return (!left && !right) || (left && right && !strcmp(left,right)); }
static bool same_resource(const qa_resource *left,const qa_resource *right)
{
    return (!left && !right) || (left && right &&
        qa_sha256_equal(qa_resource_digest(left),qa_resource_digest(right)));
}
static bool primary_source(const qa_launch_snapshot *snapshot,const qa_launch_instance *selected)
{
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    return entities && !strcmp(entities->instance,selected->selection.instance);
}
static bool same_mounts(const qa_vfs *left,const qa_vfs *right)
{
    if (!left || !right || qa_vfs_resources(left)!=qa_vfs_resources(right) ||
        qa_vfs_mount_count(left)!=qa_vfs_mount_count(right) ||
        qa_vfs_prefix_count(left)!=qa_vfs_prefix_count(right)) return false;
    for (size_t i=0;i<qa_vfs_mount_count(left);++i) {
        qa_vfs_mount_info a,b;
        if (!qa_vfs_mount_at(left,i,&a) || !qa_vfs_mount_at(right,i,&b) ||
            a.id!=b.id || a.is_archive!=b.is_archive || a.format!=b.format ||
            a.comparison!=b.comparison || a.writable!=b.writable ||
            a.user_overlay!=b.user_overlay || a.q3_demo!=b.q3_demo ||
            !same_text(qa_vfs_mount_path(left,a.id),qa_vfs_mount_path(right,b.id)) ||
            (!a.is_archive && !qa_fs_root_same_object(qa_vfs_mount_root(left,a.id),
                qa_vfs_mount_root(right,b.id))) ||
            qa_vfs_archive(left,a.id)!=qa_vfs_archive(right,b.id)) return false;
    }
    for (size_t i=0;i<qa_vfs_prefix_count(left);++i) {
        const char *a,*b; const qa_mount_id *ao,*bo; size_t ac,bc;
        if (!qa_vfs_prefix_at(left,i,&a,&ao,&ac) || !qa_vfs_prefix_at(right,i,&b,&bo,&bc) ||
            !same_text(a,b) || ac!=bc || (ac && memcmp(ao,bo,ac*sizeof(*ao)))) return false;
    }
    const qa_sha256_digest *a,*b; size_t ac,bc; bool ad,bd;
    return qa_vfs_restrictions_read(left,&a,&ac,&ad) && qa_vfs_restrictions_read(right,&b,&bc,&bd) &&
        ad==bd && ac==bc && (!ac || !memcmp(a,b,ac*sizeof(*a)));
}
static bool same_profile(const qa_launch_instance *previous,const qa_launch_instance *selected)
{
    const qa_launch_provider *a=&previous->selection,*b=&selected->selection;
    const qa_product *ap=qa_catalog_product(qa_launch_instance_catalog(previous),a->product);
    const qa_product *bp=qa_catalog_product(qa_launch_instance_catalog(selected),b->product);
    if (!ap || !bp || !same_text(ap->key,bp->key) || !same_text(ap->identity,bp->identity) ||
        !same_text(ap->directory,bp->directory) || !same_text(ap->campaign,bp->campaign) ||
        ap->family!=bp->family || ap->edition!=bp->edition || ap->builtin!=bp->builtin ||
        ap->program_kind!=bp->program_kind || !same_text(ap->program,bp->program) ||
        !same_text(a->instance,b->instance) || a->runtime!=b->runtime ||
        !same_text(a->implementation,b->implementation) || !same_text(a->artifact,b->artifact) ||
        !same_text(a->component,b->component) || a->options.size!=b->options.size ||
        (a->options.size && memcmp(a->options.data,b->options.data,a->options.size)) ||
        a->clock.kind!=b->clock.kind || a->clock.interval_ns!=b->clock.interval_ns ||
        a->clock.minimum_frame_ns!=b->clock.minimum_frame_ns ||
        a->clock.maximum_frame_ns!=b->clock.maximum_frame_ns || a->clock.maximum_steps!=b->clock.maximum_steps ||
        !same_resource(previous->artifact,selected->artifact) ||
        !same_resource(previous->declaration,selected->declaration) ||
        previous->interface_count!=selected->interface_count || previous->behavior_count!=selected->behavior_count ||
        !same_mounts(previous->content,selected->content)) return false;
    for (size_t i=0;i<previous->interface_count;++i) {
        const qa_launch_resource *x=previous->interfaces+i,*y=selected->interfaces+i;
        const qa_product *xp=qa_catalog_product(qa_launch_instance_catalog(previous),x->product);
        const qa_product *yp=qa_catalog_product(qa_launch_instance_catalog(selected),y->product);
        if (!xp || !yp || !same_text(xp->key,yp->key) || !same_text(x->path,y->path) ||
            !same_resource(x->resource,y->resource)) return false;
    }
    for (size_t i=0;i<previous->behavior_count;++i) {
        const qa_catalog_weapon_behavior *x=previous->behaviors[i],*y=selected->behaviors[i];
        if (!same_text(x->id,y->id) || x->runtime!=y->runtime || x->role!=y->role ||
            !same_text(x->artifact_path,y->artifact_path) ||
            !qa_sha256_equal(&x->declaration_digest,&y->declaration_digest) ||
            !qa_sha256_equal(&x->artifact_digest,&y->artifact_digest)) return false;
    }
    return true;
}
bool frontend_config_store_same_profile(const qa_launch_instance *previous,const qa_launch_instance *selected)
{ return previous && selected && same_profile(previous,selected); }
static frontend_config_source *previous_source(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_launch_instance *selected)
{
    const qa_launch_snapshot *published=qa_application_launch(application);
    const qa_launch_instance *old=published?qa_launch_snapshot_find(published,selected->selection.instance):NULL;
    if (!old && manager->parked_live && !published && manager->parked &&
        manager->parked->application==application) old=instance(manager->parked);
    if (!old || !same_profile(old,selected)) return NULL;
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        const qa_launch_instance *retained=instance(source);
        if (source->application!=application || (!source->published &&
            (source!=manager->parked || !manager->parked_live || published)) || source->imported ||
            !source->configured || !source->released || source->running || source->phase || !retained ||
            retained->storage!=old->storage || retained->state!=old->state ||
            source->primary!=primary_source(candidate,selected)) continue;
        const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
        if (source->primary && !manager->frontend->options.dedicated &&
            (!choices || choices->seat_count!=source->seat_count)) return NULL;
        const qa_launch_binding *movement=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER},QA_ROLE_MOVEMENT,"");
        const qa_launch_instance *movement_source=movement?qa_launch_snapshot_find(candidate,movement->instance):selected;
        if (!movement_source || (qa_console_dialect)movement_source->selection.clock.kind!=source->movement_dialect)
            return NULL;
        for (size_t i=0;i<source->seat_count;++i) {
            qa_console_dialect seat_dialect;
            if (!choices || i>=choices->seat_count || choices->seats[i].id!=source->seats[i].logical ||
                !seat_movement(candidate,source->seats[i].logical,&seat_dialect,NULL) ||
                seat_dialect!=source->seats[i].movement_dialect) return NULL;
        }
        return source;
    }
    return NULL;
}
static void variable_carries_discard(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_console *console)
{
    config_variable_carry **at=&manager->variable_carries;
    while (*at) {
        config_variable_carry *row=*at;
        if ((!application || row->application==application) &&
            (!candidate || row->candidate==candidate) && (!console || row->console==console)) {
            *at=row->next; free(row);
        } else at=&row->next;
    }
}
static bool variables_carried(const frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_launch_instance *selected,qa_console *console,qa_cvars *cvars)
{
    for (const config_variable_carry *row=manager->variable_carries;row;row=row->next)
        if (row->application==application && row->candidate==candidate && row->storage==selected->storage &&
            row->console==console && row->cvars==cvars) return true;
    return false;
}
bool frontend_config_store_carry_variables(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *authority,
    bool *carried,qa_error *error)
{
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_cvars *cvars=authority?authority->cvars:NULL;
    uint64_t cvar_owner=authority?authority->declaration_owner:0;
    bool game=authority && game_scope(authority->scope);
    if (!manager || !application || !candidate || !selected || !cvars ||
        !game || authority->scope.seat || !authority->scope.provider ||
        authority->scope.provider!=authority->command.owner || !authority->console ||
        qa_console_cvars(authority->console)!=cvars ||
        cvars==qa_application_cvars(application) || !cvar_owner || !carried ||
        qa_launch_snapshot_find(candidate,selected->selection.instance)!=selected)
        return fail(error,QA_ERROR_ARGUMENT,"GAME variable carry requires its fresh physical constructor tuple");
    *carried=false;
    frontend_config_source *fresh=frontend_config_store_source(manager,authority->console);
    if (fresh) {
        const qa_launch_instance *retained=instance(fresh);
        if (fresh->application!=application || fresh->candidate!=candidate || fresh->cvars!=cvars ||
            !retained || retained->storage!=selected->storage)
            return fail(error,QA_ERROR_ARGUMENT,"Variable carry names another prepared physical source");
        /* A completed real script phase owns its resulting values. A profile
         * carry has no script phase and still needs the factory's scalar copy. */
        if (!fresh->profile_carried) {
            if (!fresh->configured || !fresh->released || fresh->running || fresh->phase)
                return fail(error,QA_ERROR_ARGUMENT,"Variable carry cannot bypass its unfinished real configuration phase");
            return true;
        }
        if (fresh->variables_carried) { *carried=true; return true; }
    } else if (variables_carried(manager,application,candidate,selected,authority->console,cvars)) {
        *carried=true; return true;
    }
    frontend_config_source *previous=previous_source(manager,application,candidate,selected);
    if (!previous) return true;
    qa_application_console_scope old_scope;
    bool parked=previous==manager->parked;
    if (!parked && (!qa_application_console_scope_read(application,previous->console,&old_scope) ||
        old_scope.kind!=authority->scope.kind || old_scope.seat!=authority->scope.seat ||
        old_scope.provider!=authority->scope.provider))
        return fail(error,QA_ERROR_ARGUMENT,"GAME variable carry changed its actual physical source scope");
    if (!previous->cvars || cvars==previous->cvars ||
        qa_cvars_dialect(cvars)!=qa_cvars_dialect(previous->cvars) ||
        !qa_cvars_observer_idle(previous->cvars) || !qa_cvars_observer_idle(cvars))
        return fail(error,QA_ERROR_ARGUMENT,"GAME variable carry lost its actual idle private registries");
    config_variable_carry *receipt=NULL;
    if (!fresh) {
        receipt=calloc(1,sizeof(*receipt));
        if (!receipt) return fail(error,QA_ERROR_MEMORY,"Retaining the actual early GAME scalar carry");
    }
    const bool q3=qa_cvars_dialect(cvars)==QA_CONSOLE_Q3;
    for (size_t i=0;i<qa_cvars_count(previous->cvars);++i) {
        const qa_cvar_view *value=qa_cvars_at(previous->cvars,i);
        if (equal(value->name,"mapname") || equal(value->name,"sv_mapname") ||
            (q3 && ((value->flags&QA_CVAR_INIT) || equal(value->name,"sv_cheats")))) continue;
        uint64_t owner=value->owner==previous->command.owner?authority->command.owner:
            value->owner?cvar_owner:0;
        if (!qa_cvars_register(cvars,value->name,value->reset_value,value->flags,owner,value->description,error) ||
            !qa_cvars_set(cvars,value->name,value->latched_value?value->latched_value:value->value,true,error)) {
            free(receipt); return false;
        }
    }
    if (fresh) fresh->variables_carried=true;
    else {
        *receipt=(config_variable_carry){.next=manager->variable_carries,.application=application,
            .candidate=candidate,.storage=selected->storage,.console=authority->console,.cvars=cvars};
        manager->variable_carries=receipt;
    }
    *carried=true; return true;
}
static bool carry_variables(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *source,bool *carried,qa_error *error)
{ return frontend_config_store_carry_variables(context,application,candidate,source,carried,error); }
static bool program_source(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *fresh,qa_application_startup_source *out,bool *found,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !application || !candidate || !fresh || !fresh->descriptor || !out || !found ||
        (!game_scope(fresh->scope) && !client_scope(fresh->scope)) || !fresh->console || !fresh->cvars ||
        qa_launch_snapshot_find(candidate,fresh->descriptor->selection.instance)!=fresh->descriptor)
        return fail(error,QA_ERROR_ARGUMENT,"Program continuation requires its actual fresh source tuple");
    *found=false;
    if (client_scope(fresh->scope))
        return frontend_remote_config_program_source(manager->clients,application,candidate,fresh,out,found,error);
    frontend_config_source *previous=previous_source(manager,application,candidate,fresh->descriptor);
    if (!previous) return true;
    qa_application_console_scope scope;
    bool parked=previous==manager->parked;
    if (parked) scope=previous->scope;
    if ((!parked && (!qa_application_console_scope_read(application,previous->console,&scope) ||
        !same_scope(scope,fresh->scope) || !same_scope(scope,previous->scope) ||
        scope.kind!=fresh->scope.kind)) ||
        !previous->cvars || qa_console_cvars(previous->console)!=previous->cvars ||
        !qa_console_idle(previous->console) || !qa_cvars_observer_idle(previous->cvars))
        return fail(error,QA_ERROR_ARGUMENT,"Program continuation lost its true published physical source");
    qa_command_context command=previous->command;
    command.registry=command.generation=0; command.actor=(qa_actor_id){0};
    if (parked && !parked_capture(manager,&command,&command,error)) return false;
    *out=(qa_application_startup_source){.descriptor=instance(previous),.scope=scope,
        .console=previous->console,.cvars=previous->cvars,.command=command,
        .declaration_owner=previous->declaration_owner};
    *found=true; return true;
}
static bool configuration_store(void *context,qa_application *application,
    const qa_application_startup_source *authority,qa_settings_store *out,qa_error *error)
{
    frontend_config_source *source=authority?frontend_config_store_source(context,authority->console):NULL;
    const qa_launch_instance *selected=authority?authority->descriptor:NULL,*retained=source?instance(source):NULL;
    qa_application_console_scope scope;
    if (!source || !out || source->application!=application || !source->published ||
        !source->configured || !source->released || !selected || !retained ||
        selected->storage!=retained->storage || selected->state!=retained->state ||
        source->cvars!=authority->cvars || authority->scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME ||
        !qa_application_console_scope_read(application,source->console,&scope) ||
        scope.provider!=authority->scope.provider || scope.kind!=authority->scope.kind || scope.seat!=authority->scope.seat)
        return fail(error,QA_ERROR_ARGUMENT,"Campaign configuration lost its published physical ConfigStore owner");
    qa_settings_store store=frontend_config_files_store(source->files,false);
    if (!store.vfs || !store.mount || !qa_vfs_mount_root(store.vfs,store.mount))
        return fail(error,QA_ERROR_ARGUMENT,"Campaign ConfigStore has no retained writable directory");
    *out=store; return true;
}
static bool carry(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *authority,
    frontend_config_source **out,qa_error *error)
{
    const qa_launch_instance *selected=authority->descriptor;
    qa_console *console=authority->console; qa_cvars *cvars=authority->cvars;
    const qa_command_context *command=&authority->command;
    frontend_config_source *previous=previous_source(manager,application,candidate,selected);
    if (!previous || !previous->cvars || qa_cvars_dialect(previous->cvars)!=qa_cvars_dialect(cvars) ||
        !qa_cvars_observer_idle(previous->cvars) || !qa_console_idle(previous->console))
        return fail(error,QA_ERROR_ARGUMENT,"Source carry lost its genuine live previous configuration owner");
    frontend_config_source *source=calloc(1,sizeof(*source));
    if (!source) return fail(error,QA_ERROR_MEMORY,"Retaining carried source configuration");
    source->manager=manager; source->application=application; source->candidate=candidate;
    source->console=console; source->cvars=cvars; source->command=*command;
    source->scope=authority->scope; source->declaration_owner=authority->declaration_owner;
    source->primary=previous->primary; source->configured=source->released=true;
    source->profile_carried=true;
    source->variables_carried=variables_carried(manager,application,candidate,selected,console,cvars);
    source->movement_dialect=previous->movement_dialect; source->has_mod=previous->has_mod;
    bool ok=qa_launch_instance_retain_metadata(selected,&source->metadata,error) &&
        frontend_config_files_clone(previous->files,&source->files,error) &&
        registry_carry(source,previous->movement,&source->movement,error);
    if (ok && previous->fallback==previous->movement) source->fallback=source->movement;
    else if (ok) ok=registry_carry(source,previous->fallback,&source->fallback,error);
    if (ok && previous->keys) {
        ok=frontend_keys_carry(manager->frontend->keys,previous->keys,source->files,cvars,&source->keys,error) &&
            frontend_key_profile_scope(source->keys,(qa_application_console_scope){(qa_actor_owner)command->owner,
                QA_APPLICATION_CONSOLE_Q3_GAME,0},cvars,error);
    }
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    for (size_t i=0;ok && i<previous->seat_count;++i) {
        if (!choices || i>=choices->seat_count || choices->seats[i].id!=previous->seats[i].logical) {
            ok=fail(error,QA_ERROR_ARGUMENT,"Source carry changed its actual authored seat profile"); break;
        }
        const config_seat *old_seat=previous->seats+i;
        config_seat *seat=source->seats+source->seat_count++; seat->logical=old_seat->logical;
        if (!seat_movement(candidate,seat->logical,&seat->movement_dialect,error)) { ok=false; break; }
        ok=registry_carry(source,old_seat->cvars,&seat->cvars,error) && registry_carry(source,old_seat->mouse,&seat->mouse,error) &&
            frontend_authored_bindings_clone(old_seat->authored,&seat->authored,error);
        qa_input_seat *active=frontend_config_source_input(previous,seat->logical);
        qa_command_context seat_command=*command; seat_command.origin=QA_COMMAND_SEAT; seat_command.seat=seat->logical;
        ok=ok && active && qa_application_capture_command_context(application,&seat_command,&seat_command,error);
        qa_input_seat_options options={.context=seat_command,.console=console,.cvars=seat->mouse,
            .gamepad=active?*qa_input_seat_gamepad_tuning(active):qa_gamepad_defaults(),
            .seat=(uint32_t)i,.context_ready=input_context,.context_user=source};
        if (ok) { seat->input=qa_input_seat_create(&options,error); ok=seat->input!=NULL; }
        if (ok) ok=qa_input_seat_profile(seat->input,seat->movement_dialect,error) &&
            qa_input_settings_register(seat->mouse,(qa_movement_kind)seat->movement_dialect,error);
        size_t count=active?qa_input_seat_binding_count(active):0;
        qa_input_binding *bindings=ok && count<=SIZE_MAX/sizeof(*bindings)?malloc(count?count*sizeof(*bindings):1):NULL;
        if (ok && !bindings) ok=fail(error,QA_ERROR_MEMORY,"Retaining actual carried logical bindings");
        for (size_t n=0;ok && n<count;++n) bindings[n]=*qa_input_seat_binding_at(active,n);
        if (ok) ok=qa_input_seat_replace_bindings(seat->input,bindings,count,error);
        free(bindings);
        qa_buffer settings={0};
        if (ok && old_seat->found) ok=qa_seat_settings_encode(&old_seat->settings,&settings,error) &&
            qa_seat_settings_parse((qa_bytes){settings.data,settings.size},&seat->settings,error);
        qa_buffer_free(&settings); seat->found=old_seat->found;
        if (ok) ok=selected_defaults(source,seat,false,error);
    }
    if (ok && previous->dedicated_bindings)
        ok=frontend_config_bindings_clone(previous->dedicated_bindings,&source->dedicated_bindings,error);
    if (ok) ok=install_commands(source,error);
    if (!ok) { qa_error cleanup={0}; if (!source_destroy(source,&cleanup) && error) *error=cleanup; return false; }
    source->next=manager->sources; manager->sources=source; *out=source; return true;
}
static bool parked_parent(const frontend_config_store *manager)
{
    qa_frontend *f=manager?manager->frontend:NULL;
    return f && f->application && manager->parked_console && manager->parked_cvars &&
        manager->parked_root==qa_application_console(f->application) &&
        qa_console_cvars(manager->parked_console)==manager->parked_cvars;
}
static bool parked_capture(void *context,const qa_command_context *source,
    qa_command_context *out,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!source || !out || !parked_parent(manager) ||
        source->owner!=manager->parked_basis.scope.provider || source->origin==QA_COMMAND_REMOTE ||
        source->dialect!=manager->parked_basis.command.dialect)
        return fail(error,QA_ERROR_ARGUMENT,"Stopped Source command lost its actual ENGINE configuration namespace");
    qa_command_context engine=*source;
    engine.owner=0; engine.registry=engine.generation=0; engine.actor=(qa_actor_id){0};
    if (!qa_application_capture_command_context(manager->frontend->application,&engine,&engine,error)) return false;
    engine.owner=source->owner;
    *out=engine; return true;
}
static bool parked_active(void *context,const qa_command_context *command)
{
    frontend_config_store *manager=context;
    if (!command || !parked_parent(manager) || command->actor.registry ||
        command->owner!=manager->parked_basis.scope.provider || command->origin==QA_COMMAND_REMOTE ||
        command->dialect!=manager->parked_basis.command.dialect) return false;
    qa_command_context engine=*command; engine.owner=0;
    return qa_application_command_context_active(manager->frontend->application,&engine);
}
bool frontend_config_store_parked_current(const frontend_config_store *manager,
    const qa_application_startup_source *source)
{
    const frontend_config_source *parked=manager?manager->parked:NULL;
    return parked && source && manager->parked_live && parked_parent(manager) &&
        !qa_application_launch(manager->frontend->application) && source->scope.kind==QA_APPLICATION_CONSOLE_ENGINE &&
        source->console==parked->console && source->cvars==parked->cvars && source->descriptor &&
        source->descriptor->storage==instance(parked)->storage && same_scope(source->scope,parked->scope) &&
        parked_active((void *)manager,&source->command);
}
bool frontend_config_store_parked_recipient(const frontend_config_store *manager,
    const qa_application_startup_source *source,uint32_t *physical)
{
    if (!physical || !frontend_config_store_parked_current(manager,source) || !manager->parked_local ||
        manager->parked_physical>=manager->frontend->options.seats) return false;
    *physical=manager->parked_physical; return true;
}
static bool parked_program_current(void *context,qa_application *application,
    const qa_application_startup_source *source,qa_application_console_scope *origin,
    uint64_t *generation,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || manager->frontend->application!=application ||
        !frontend_config_store_parked_current(manager,source))
        return fail(error,QA_ERROR_ARGUMENT,"Command programme lost its actual parked ENGINE owner");
    if (origin) *origin=manager->parked_basis.scope;
    if (generation) *generation=manager->parked_basis.command.generation;
    return true;
}
static void parked_print(void *context,const qa_command_context *command,const char *text)
{ frontend_console_print(((frontend_config_store *)context)->frontend,command,text); }
static bool parked_read(void *context,const qa_command_context *command,const char *path,
    qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_store *manager=context;
    return manager->parked && parked_active(manager,command) &&
        frontend_config_files_console_read(manager->parked->files,path,command,bytes,lease,error);
}
static void parked_release(void *context,void *lease)
{
    frontend_config_store *manager=context;
    if (manager->parked) frontend_config_files_release(manager->parked->files,lease);
}
static qa_command_result parked_command(void *context,const qa_command_invocation *call,qa_error *error)
{
    frontend_config_store *manager=context;
    qa_application_startup_source source; bool present=false,handled=false;
    if (!frontend_config_store_primary_server_read(manager,&source,&present,error) || !present ||
        !frontend_config_store_parked_current(manager,&source) || call->console!=source.console ||
        !frontend_commands_source(manager->frontend,&source,call,&handled,error)) return QA_COMMAND_FAILED;
    return handled?QA_COMMAND_HANDLED:QA_COMMAND_UNHANDLED;
}
static bool park_identity(void *context,qa_console_program_identity kind,uint64_t source,
    uint64_t *target,qa_error *error)
{ (void)context; (void)kind; (void)error; *target=source; return true; }
static bool park_context(void *context,const qa_command_context *source,qa_command_context *target,qa_error *error)
{ return parked_capture(context,source,target,error); }
static bool park_published_context(void *context,const qa_command_context *source,
    const qa_command_context *prepared,qa_command_context *target,qa_error *error)
{ (void)source; return parked_capture(context,prepared,target,error); }
static bool park_current(void *context,const qa_console *source,const qa_console *target,qa_error *error)
{
    frontend_config_store *manager=context;
    return (parked_parent(manager) && manager->parked_from &&
        source==manager->parked_from->console && target==manager->parked_console) ||
        fail(error,QA_ERROR_ARGUMENT,"Source programme transfer lost its physical returned console pair");
}
static bool park_published(void *context,const qa_command_context *basis,
    const qa_console *target,qa_error *error)
{
    frontend_config_store *manager=context;
    return (basis && parked_parent(manager) && target==manager->parked_console &&
        !qa_application_launch(manager->frontend->application)) ||
        fail(error,QA_ERROR_ARGUMENT,"Parked ENGINE programme requires completed server retirement");
}
static bool parked_seal(frontend_config_store *manager,frontend_config_source *source,qa_error *error)
{
    if (manager->parked_program) {
        if (manager->parked_sealed) return true;
        if (!qa_console_program_abort(manager->parked_program,error)) return false;
        manager->parked_program=NULL;
    }
    qa_buffer bytes={0}; qa_cvars_restore *values=NULL;
    bool ok=qa_cvars_save_capture(source->cvars,&bytes,error) &&
        qa_cvars_save_prepare(manager->parked_cvars,(qa_bytes){bytes.data,bytes.size},&values,error);
    if (ok) ok=qa_cvars_save_commit(values,error);
    if (!ok) qa_cvars_save_abort(values);
    qa_buffer_free(&bytes);
    if (!ok) return false;
    qa_console_program_resolvers resolve={.context=manager,.identity=park_identity,
        .command_context=park_context,.published_context=park_published_context,
        .published_candidate_context=park_context,.current=park_current,.published=park_published};
    manager->parked_program=qa_console_program_prepare(source->console,manager->parked_console,&resolve,error);
    if (!manager->parked_program) return false;
    bool transferred=qa_console_program_preflight(manager->parked_program,error) &&
        qa_console_program_seal(manager->parked_program,error);
    manager->parked_sealed=transferred;
    if (!transferred) {
        qa_error issue=error?*error:(qa_error){0};
        if (!qa_console_program_abort(manager->parked_program,error)) return false;
        manager->parked_program=NULL;
        if (error) *error=issue;
    }
    return transferred;
}
bool frontend_config_store_park_server(frontend_config_store *manager,
    const qa_application_startup_source *authority,qa_error *error)
{
    if (!manager || !authority || !manager->frontend->server_stop_owner ||
        manager->frontend->server_stop_owner!=authority->scope.provider ||
        !qa_console_idle(authority->console) || qa_console_pending(authority->console))
        return fail(error,QA_ERROR_ARGUMENT,"ENGINE profile adoption requires its returned Source stop request");
    if (manager->parked) return manager->parked_from && manager->parked_from->console==authority->console;
    frontend_config_source *source=frontend_config_store_source(manager,authority->console);
    if (!source || !source->primary || !source->published || source->cvars!=authority->cvars ||
        !instance(source) || instance(source)->storage!=authority->descriptor->storage)
        return fail(error,QA_ERROR_ARGUMENT,"ENGINE profile adoption lost its actual published Source configuration");
    manager->parked_basis=*authority;
    manager->parked_basis.command.script=NULL;
    manager->parked_root=qa_application_console(source->application);
    manager->parked_from=source;
    manager->parked_local=frontend_command_seat_read(manager->frontend,&authority->command,&manager->parked_physical);
    qa_cvar_options variables={.dialect=qa_cvars_dialect(authority->cvars)};
    manager->parked_cvars=qa_cvars_create(&variables,error);
    qa_console_options options={.context=manager->parked_basis.command,.cvars=manager->parked_cvars,
        .user=manager,.print=parked_print,.source_command=parked_command,.read_script=parked_read,
        .release_script=parked_release,.capture_context=parked_capture,.context_active=parked_active};
    if (manager->parked_cvars) manager->parked_console=qa_console_create(&options,error);
    qa_application_startup_source target=*authority;
    target.console=manager->parked_console; target.cvars=manager->parked_cvars;
    bool ok=target.console && carry(manager,source->application,qa_application_launch(source->application),
        &target,&manager->parked,error);
    if (!ok) {
        qa_console_destroy(manager->parked_console); qa_cvars_destroy(manager->parked_cvars);
        manager->parked_console=NULL; manager->parked_cvars=NULL; manager->parked_root=NULL; manager->parked_from=NULL;
        return false;
    }
    manager->parked->scope.kind=QA_APPLICATION_CONSOLE_ENGINE;
    manager->parked->candidate=NULL;
    manager->parked_basis.descriptor=instance(manager->parked);
    return true;
}
bool frontend_config_store_parked_finish(frontend_config_store *manager,qa_error *error)
{
    if (!manager || !manager->parked) return true;
    if (!qa_application_launch(manager->frontend->application)) {
        if (manager->parked_live) return true;
        if (!manager->parked_program || !qa_console_program_adopt(manager->parked_program,error)) return false;
        manager->parked_program=NULL; manager->parked_live=true;
        qa_cvars_set_server_active(manager->parked_cvars,false);
        return true;
    }
    return frontend_config_store_parked_release(manager,error);
}
bool frontend_config_store_parked_release(frontend_config_store *manager,qa_error *error)
{
    if (!manager || !manager->parked) return true;
    frontend_config_source *source=manager->parked;
    frontend_config_source *next=source->next;
    frontend_config_source **at=&manager->sources;
    while (*at && *at!=source) at=&(*at)->next;
    if (*at!=source || !source_destroy(source,error)) return false;
    *at=next;
    return true;
}
static void discard_retained_input(frontend_config_store *manager)
{
    for (size_t i=0;i<manager->input_count;++i) {
        config_seat *seat=manager->input_seats+i;
        qa_input_seat_destroy(seat->input);
        frontend_authored_bindings_destroy(seat->authored);
        *seat=(config_seat){0};
    }
    manager->input_source=NULL; manager->input_candidate=NULL; manager->input_application=NULL;
    manager->input_count=0; manager->input_prepared=false;
}
static bool same_input_role(const qa_launch_snapshot *previous,const qa_launch_snapshot *candidate,
    uint32_t logical,qa_launch_role role)
{
    const qa_launch_binding *a=qa_launch_binding_for(qa_launch_snapshot_choices(previous),
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=logical},role,"");
    const qa_launch_binding *b=qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=logical},role,"");
    if (!a || !b) return !a && !b;
    const qa_launch_instance *old=qa_launch_snapshot_find(previous,a->instance);
    const qa_launch_instance *next=qa_launch_snapshot_find(candidate,b->instance);
    return old && next && old->storage==next->storage && old->state==next->state;
}
static bool prepare_retained_input(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,qa_error *error)
{
    if (!candidate || manager->frontend->options.dedicated || frontend_network_remote(manager->frontend)) return true;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    const qa_launch_binding *entities=qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(candidate,entities->instance):NULL;
    frontend_config_source *source=published_primary(manager,application);
    const qa_launch_instance *held=source?instance(source):NULL;
    if (!selected || !held || selected->storage!=held->storage || selected->state!=held->state) return true;
    qa_frontend *f=manager->frontend;
    const qa_launch_snapshot *previous=qa_application_launch(application);
    bool changed=false;
    for (size_t i=0;i<source->seat_count;++i) {
        uint32_t logical=source->seats[i].logical;
        if (!same_input_role(previous,candidate,logical,QA_ROLE_MOVEMENT) ||
            !same_input_role(previous,candidate,logical,QA_ROLE_ARSENAL)) changed=true;
    }
    if (!changed && !manager->input_source) return true;
    if (manager->input_source) {
        return (manager->input_source==source && manager->input_candidate==candidate &&
            manager->input_application==application && manager->input_prepared) ||
            fail(error,QA_ERROR_ARGUMENT,"Retained input staging lost its completed actual candidate");
    }
    if (manager->restoring || manager->running || !source->configured || !source->released ||
        source->imported || source->running || source->phase || !qa_console_idle(source->console) ||
        !choices || choices->seat_count<source->seat_count || source->seat_count!=f->options.seats || !f->seats ||
        qa_application_startup_candidate(application)!=candidate ||
        !qa_application_startup_resource_phase(application,candidate))
        return fail(error,QA_ERROR_ARGUMENT,"Retained input staging requires its actual reused primary source and release phase");
    manager->input_source=source; manager->input_candidate=candidate; manager->input_application=application;
    for (size_t i=0;i<source->seat_count;++i) {
        const config_seat *old=source->seats+i;
        config_seat *seat=manager->input_seats+manager->input_count++;
        qa_input_seat *active=f->seats[i].input;
        if (!choices->seats[i].local || choices->seats[i].bot || choices->seats[i].id!=old->logical ||
            !active || qa_input_seat_ordinal(active)!=i)
            return fail(error,QA_ERROR_ARGUMENT,"Retained input staging changed its actual authored physical seat");
        seat->logical=old->logical;
        if (!seat_movement(candidate,seat->logical,&seat->movement_dialect,error) ||
            !qa_input_seat_configuration_clone(active,&seat->input,error) ||
            !qa_input_seat_profile(seat->input,seat->movement_dialect,error) ||
            !frontend_authored_bindings_clone(old->authored,&seat->authored,error)) return false;
        qa_command_context command=qa_input_seat_context(active);
        command.registry=command.generation=0; command.actor=(qa_actor_id){0};
        command.dialect=seat->movement_dialect;
        if (!qa_input_seat_context_ready(seat->input,&command,error)) return false;
        qa_input_seat_context_publish(seat->input,&command);
        frontend_config_weapon_catalog catalog;
        qa_strings *strings=qa_session_strings(qa_application_session(application));
        int32_t controller=f->input?qa_input_platform_controller(f->input,(unsigned)i):-1;
        if (!frontend_config_weapon_defaults(application,candidate,
            (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=seat->logical},strings,&catalog,error) ||
            !frontend_authored_bindings_select(seat->authored,seat->input,seat->movement_dialect,
                strings,catalog.items,catalog.count,controller<0?0:controller,error)) return false;
    }
    manager->input_prepared=true; return true;
}
static bool prepare(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *authority,void **phase,qa_error *error)
{
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_console *console=authority?authority->console:NULL; qa_cvars *cvars=authority?authority->cvars:NULL;
    const qa_command_context *command=authority?&authority->command:NULL;
    frontend_config_store *manager=context; qa_frontend *f=manager->frontend;
    if (authority && client_scope(authority->scope))
        return frontend_remote_config_prepare(manager->clients,application,candidate,authority,phase,error);
    if (!phase || *phase || !selected || !console || !cvars || !command || !game_scope(authority->scope) ||
        authority->scope.provider!=command->owner || frontend_config_store_source(manager,console))
        return fail(error,QA_ERROR_ARGUMENT,"Source configuration requires its fresh actual console and registry");
    if (previous_source(manager,application,candidate,selected)) {
        frontend_config_source *carried=NULL;
        if (!carry(manager,application,candidate,authority,&carried,error)) return false;
        *phase=carried;
        return frontend_config_store_shared_begin(manager,application,candidate,authority,error);
    }
    frontend_config_source *source=calloc(1,sizeof(*source));
    if (!source) return fail(error,QA_ERROR_MEMORY,"Retaining source configuration");
    source->manager=manager; source->application=application; source->candidate=candidate;
    source->console=console; source->cvars=cvars; source->command=*command;
    source->scope=authority->scope; source->declaration_owner=authority->declaration_owner;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    source->primary=entities && !strcmp(entities->instance,selected->selection.instance);
    qa_catalog *catalog=qa_launch_instance_catalog(selected);
    const qa_product *product=qa_catalog_product(catalog,selected->selection.product);
    qa_product_id base_id=product?qa_catalog_configuration_base(catalog,product->id):QA_PRODUCT_NONE;
    const qa_product *base=base_id?qa_catalog_product(catalog,base_id):product;
    source->has_mod=product && base && strcmp(product->directory,base->directory)!=0;
    bool ok=product && qa_source_frame_time_register(cvars,authority->declaration_owner,error) &&
        qa_launch_instance_retain_metadata(selected,&source->metadata,error);
    if (ok) source->files=frontend_config_files_create(catalog,product->id,
        frontend_global_settings_storage_user_store(f->global_settings_storage),
        frontend_global_settings_storage_device_store(f->global_settings_storage),error);
    ok=ok && source->files;
    if (ok && source->primary) ok=frontend_input_profile_bind_store(f,catalog,product->id,
        frontend_config_files_store(source->files,false),error);
    if (ok && product->family==QA_GAME_Q3) {
        qa_q3_product_policy policy;
        ok=f->keys && qa_application_q3_product_policy_read(application,&policy) &&
            frontend_keys_prepare(f->keys,source->files,cvars,&policy,f->options.dedicated,&source->keys,error);
        if (ok) ok=frontend_key_profile_scope(source->keys,
            (qa_application_console_scope){.provider=(qa_actor_owner)command->owner,.kind=QA_APPLICATION_CONSOLE_Q3_GAME},cvars,error);
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
    if (ok && source->primary)
        ok=qa_settings_load_cvars(store,source_owner,3,command->dialect,&source->source_archive,error);
    if (ok && source->primary && !f->options.dedicated) {
        const char *movement_owner[2]={"movement",dialects[source->movement_dialect]};
        const char *fallback_owner[2]={"fallback",dialects[command->dialect]};
        ok=qa_settings_load_cvars(input_store(source),movement_owner,2,source->movement_dialect,&source->movement_archive,error) &&
            qa_settings_load_cvars(input_store(source),fallback_owner,2,command->dialect,&source->fallback_archive,error);
    }
    for (unsigned i=0;ok && source->primary && !f->options.dedicated && i<f->options.seats;++i) {
        if (i>=choices->seat_count) { ok=fail(error,QA_ERROR_ARGUMENT,"Configuration source lacks its actual authored local seat"); break; }
        config_seat *seat=source->seats+source->seat_count++;
        seat->logical=choices->seats[i].id;
        seat->authored=frontend_authored_bindings_create(error);
        if (!seat->authored) { ok=false; break; }
        if (!seat_movement(candidate,seat->logical,&seat->movement_dialect,error)) { ok=false; break; }
        if (!selected_defaults(source,seat,true,error)) { ok=false; break; }
        seat->cvars=registry(source,command->dialect,error); seat->mouse=registry(source,command->dialect,error);
        if (seat->cvars && command->dialect==QA_CONSOLE_Q3) {
            qa_q3_product_policy policy;
            ok=qa_application_q3_product_policy_read(application,&policy) &&
                qa_q3_product_policy_register_source(&policy,seat->cvars,0,error);
            if (!ok && (!error || error->code==QA_OK))
                fail(error,QA_ERROR_ARGUMENT,"Prepared client lost its resolved immutable Q3 product policy");
        }
        qa_command_context seat_command=*command; seat_command.origin=QA_COMMAND_SEAT; seat_command.seat=seat->logical;
        ok=ok && qa_application_capture_command_context(application,&seat_command,&seat_command,error);
        qa_input_seat_options options={.context=seat_command,.console=console,.cvars=seat->mouse,.gamepad=qa_gamepad_defaults(),
            .seat=i,.context_ready=input_context,.context_user=source};
        if (ok && seat->cvars && seat->mouse) seat->input=qa_input_seat_create(&options,error);
        char index[16],path[64]; snprintf(index,sizeof(index),"%" PRIu32,seat->logical);
        snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)seat->logical+1);
        const char *client_owner[4]={"client",product->key,selected->selection.implementation,index};
        const char *mouse_owner[3]={"input",dialects[command->dialect],index};
        const char *model=command->dialect==QA_CONSOLE_Q3?
            f->options.character_model?f->options.character_model:"sarge":
            f->options.character_model && (!f->options.character || !strcmp(f->options.character,"q2"))?
                f->options.character_model:"male";
        ok=seat->input && frontend_config_userinfo_register(seat->cvars,seat->logical,model,error) &&
            qa_input_seat_profile(seat->input,seat->movement_dialect,error) &&
            qa_input_settings_register(seat->mouse,(qa_movement_kind)seat->movement_dialect,error) &&
            qa_settings_load_seat(input_store(source),path,&seat->settings,&seat->found,error) &&
            qa_settings_load_cvars(store,client_owner,4,command->dialect,&seat->client_archive,error) &&
            qa_settings_load_cvars(input_store(source),mouse_owner,3,command->dialect,&seat->mouse_archive,error);
        if (ok && product->builtin && product->program_kind==QA_PROGRAM_BUILTIN &&
            (product->family==QA_GAME_Q1 || product->family==QA_GAME_Q2))
            ok=frontend_legacy_source_register(seat->cvars,command->dialect,source->declaration_owner,error);
    }
    if (ok) ok=install_commands(source,error);
    if (ok && source->primary) ok=phase_create(source,error);
    if (!ok) { qa_error cleanup={0}; if (!source_destroy(source,&cleanup) && error) *error=cleanup; return false; }
    source->next=manager->sources; manager->sources=source;
    *phase=source;
    return frontend_config_store_shared_begin(manager,application,candidate,authority,error);
}
static bool advance(void *context,void *phase,qa_console *console,bool *complete,qa_error *error)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase))
        return frontend_remote_config_advance(phase,console,complete,error);
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || source->console!=console || source->running || !complete)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration frame has another actual source owner");
    *complete=false;
    if (source->released) {
        if (!source->configured || source->phase)
            return fail(error,QA_ERROR_ARGUMENT,"Carried configuration has no completed actual source state");
        *complete=true; return true;
    }
    if (!source->primary) { source->configured=true; *complete=true; return true; }
    if (!source->initial_variables) {
        if (!manager->shared_seeded || !replay(source,error)) return false;
        source->initial_variables=true;
    }
    source->running=true; bool done=false;
    bool ok=frontend_startup_config_advance(source->phase,console,&done,error);
    source->running=false;
    if (source->observation_failure.code!=QA_OK) {
        if (error) *error=source->observation_failure;
        return false;
    }
    if (!ok || !done) return ok;
    if (source->seat_count) frontend_authored_bindings_finish(source->seats[source->seat_index].authored);
    if (source->seat_count && source->seat_index+1<source->seat_count) {
        if (!frontend_startup_config_destroy(source->phase,error)) return false;
        source->phase=NULL; ++source->seat_index; source->configured=false;
        config_seat *seat=source->seats+source->seat_index;
        if (!frontend_authored_bindings_secondary(seat->authored,source->seats[0].authored,
            seat->input,seat->movement_dialect,error)) return false;
        if (published_primary(manager,source->application) && !selected_defaults(source,seat,false,error)) return false;
        if (!phase_create(source,error)) return false;
        return true;
    }
    *complete=true; return true;
}
static bool phase_read(void *context,void *phase,const qa_command_context *command,const char *name,
    qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase))
        return frontend_remote_config_script_read(phase,command,name,bytes,lease,error);
    frontend_config_source *source=phase;
    if (!source || source->manager!=context) return fail(error,QA_ERROR_ARGUMENT,"Script read has another configuration owner");
    return source->phase?frontend_startup_config_read(source->phase,command,name,bytes,lease,error):
        frontend_config_files_console_read(source->files,name,command,bytes,lease,error);
}
static void phase_release(void *context,void *phase,void *lease)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase)) { frontend_remote_config_script_release(phase,lease); return; }
    frontend_config_source *source=phase;
    if (source && source->manager==context) {
        if (source->phase) frontend_startup_config_release(source->phase,lease); else release(source,lease);
    }
}
static void phase_complete(void *context,void *phase,const qa_command_context *command,const char *name,bool success)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase)) {
        frontend_remote_config_script_complete(phase,command,name,success); return;
    }
    frontend_config_source *source=phase;
    if (source && source->manager==context && source->phase)
        frontend_startup_config_script_complete(source->phase,command,name,success);
}
static bool allow(void *context,void *phase,const qa_command_invocation *command)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase)) return frontend_remote_config_allow(phase,command);
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || !source_context(source,&command->context)) return false;
    if (source->observation_failure.code!=QA_OK) return false;
    if (source->seat_count && command->argc) {
        size_t ordinal=command->context.origin==QA_COMMAND_SEAT?seat_index(source,command->context.seat):source->seat_index;
        if (ordinal<source->seat_count && !frontend_authored_bindings_observe(source->seats[ordinal].authored,
            command,&source->observation_failure)) return false;
    }
    if (!source->phase || !frontend_startup_config_restrict_shared(source->phase) || !command->argc) return true;
    const char *name=command->argv[0];
    if (equal(name,"cvar_restart")) { print(source,"Ignoring shared cvar restart in saved secondary-seat configuration.\n"); return false; }
    bool setter=equal(name,"set") || equal(name,"seta") || equal(name,"sets") || equal(name,"setu") ||
        equal(name,"toggle") || equal(name,"reset");
    const char *target=setter?(command->argc>1?command->argv[1]:NULL):name;
    if (!target) return true;
    qa_cvars *owner=frontend_config_store_cvar_owner(source->manager,source->console,&command->context,target);
    if (!setter && (!owner || !qa_cvars_find(owner,target))) return true;
    size_t ordinal=seat_index(source,command->context.seat);
    if (ordinal<source->seat_count &&
        (owner==source->seats[ordinal].cvars || owner==source->seats[ordinal].mouse)) return true;
    print(source,"Ignoring shared cvar in saved secondary-seat configuration; use autoexec.cfg for shared overrides.\n");
    return false;
}
static bool phase_destroy(void *context,void *phase,qa_error *error)
{
    frontend_config_store *manager=context;
    if (frontend_remote_config_phase(manager->clients,phase)) return frontend_remote_config_release_phase(phase,error);
    frontend_config_source *source=phase;
    if (!source || source->manager!=context || source->running) return fail(error,QA_ERROR_ARGUMENT,"Configuration phase is still executing");
    if (!frontend_startup_config_destroy(source->phase,error)) return false;
    source->phase=NULL; source->released=true; return true;
}
static bool prepare_input_publication(qa_frontend *f,config_seat *seat,size_t ordinal,qa_error *error)
{
    qa_input_seat *active=f->seats && ordinal<f->options.seats?f->seats[ordinal].input:NULL;
    if (!active || !seat->input || qa_input_seat_has_held(seat->input))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration candidate requires an isolated staged input seat");
    int32_t controller=f->input?qa_input_platform_controller(f->input,(unsigned)ordinal):-1;
    if ((controller>=0 && !qa_input_seat_remap_controller(seat->input,controller,error)) ||
        !qa_input_seat_configuration_ready(active,seat->input,error)) return false;
    qa_command_context next=qa_input_seat_context(active);
    next.registry=next.generation=0; next.actor=(qa_actor_id){0};
    next.origin=QA_COMMAND_SEAT; next.seat=seat->logical; next.dialect=seat->movement_dialect;
    qa_seat_console *console=f->seats[ordinal].console;
    if (!console || !qa_input_seat_context_ready(active,&next,error) ||
        !qa_seat_console_context_ready(console,&next,error)) return false;
    seat->publication_command=next; seat->publication_input=active; seat->publication_console=console;
    return true;
}
static bool prepare_candidate(void *context,qa_application *application,const qa_launch_snapshot *candidate,qa_error *error)
{
    frontend_config_store *manager=context; qa_frontend *f=manager->frontend;
    if (!candidate) {
        if (manager->running || manager->prepared || manager->prepared_application || manager->key_publication.owner ||
            !root_current(manager) || qa_application_launch(application) || !frontend_config_store_shared(manager,application,NULL))
            return fail(error,QA_ERROR_ARGUMENT,"Bootstrap preflight lost its actual source-free ENGINE root");
        return true;
    }
    if (manager->running || !application || !candidate ||
        (manager->prepared && (manager->prepared!=candidate || manager->prepared_application!=application)))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration publication already has another actual candidate");
    if (manager->prepared) {
        if ((f->keys && !frontend_keys_publication_current(f->keys,&manager->key_publication)) ||
            (!f->keys && manager->key_publication.owner))
            return fail(error,QA_ERROR_ARGUMENT,"Repeated configuration preflight lost its unconsumed key ticket");
        frontend_keys_publication_discard(&manager->key_publication);
        manager->prepared=NULL; manager->prepared_primary=NULL; manager->prepared_application=NULL;
    } else if (manager->key_publication.owner || manager->prepared_application || manager->prepared_primary)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration preflight retains a stale publication owner");
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        if (source->application!=application || source->published || source->candidate!=candidate) continue;
        for (size_t i=0;i<source->seat_count;++i) {
            source->seats[i].publication_input=NULL; source->seats[i].publication_console=NULL;
            source->seats[i].publication_command=(qa_command_context){0};
        }
    }
    if (!frontend_remote_configs_ready(manager->clients,application,candidate,error)) return false;
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        if (source->application!=application || source->published || source->candidate!=candidate) continue;
        if (!source->configured || source->running || !qa_console_idle(source->console) ||
            source->observation_failure.code!=QA_OK || qa_console_pending(source->console) || !frontend_config_files_idle(source->files))
            return fail(error,QA_ERROR_ARGUMENT,"Configuration publication requires all returned actual source phases");
    }
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *selected=entities?qa_launch_snapshot_find(candidate,entities->instance):NULL;
    if (!entities) {
        if (f->keys && !frontend_keys_publication_ready(f->keys,NULL,NULL,&manager->key_publication,error)) return false;
        manager->prepared=candidate; manager->prepared_primary=NULL; manager->prepared_application=application;
        return true;
    }
    qa_console *primary_console=NULL; qa_cvars *primary_cvars=NULL; qa_command_context primary_command;
    if (!selected || !qa_application_startup_source_read(application,candidate,selected,
        &primary_console,&primary_cvars,&primary_command,error)) return false;
    frontend_config_source *primary=frontend_config_store_source(manager,primary_console);
    if (!primary || primary->application!=application || primary->cvars!=primary_cvars ||
        !source_context(primary,&primary_command))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration publication lost its exact physical primary source");
    for (size_t i=0;!primary->published && i<primary->seat_count;++i)
        if (!prepare_input_publication(f,primary->seats+i,i,error)) return false;
    if (manager->input_source) {
        if (manager->input_source!=primary || manager->input_candidate!=candidate ||
            manager->input_application!=application || !manager->input_prepared)
            return fail(error,QA_ERROR_ARGUMENT,"Retained input preflight changed its actual primary source");
        for (size_t i=0;i<manager->input_count;++i)
            if (!prepare_input_publication(f,manager->input_seats+i,i,error)) return false;
    }
    if (f->keys && !frontend_keys_publication_ready(f->keys,primary->keys,
        primary->keys?primary->cvars:NULL,&manager->key_publication,error)) return false;
    manager->prepared=candidate; manager->prepared_primary=primary; manager->prepared_application=application; return true;
}
static bool preinit(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *authority,qa_error *error)
{
    frontend_config_store *manager=context;
    if (authority && client_scope(authority->scope))
        return frontend_remote_config_preinit(manager->clients,application,candidate,authority,error) &&
            refresh_source(manager,application,candidate,authority,error);
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_console *console=authority?authority->console:NULL; qa_cvars *cvars=authority?authority->cvars:NULL;
    const qa_command_context *command=authority?&authority->command:NULL;
    if (!authority || !game_scope(authority->scope))
        return fail(error,QA_ERROR_ARGUMENT,"Source Init has no configured actual GAME scope");
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!source && selected && command &&
        !carry(context,application,candidate,authority,&source,error)) return false;
    const qa_launch_instance *retained=source?instance(source):NULL;
    if (!source || source->application!=application || source->candidate!=candidate ||
        source->cvars!=cvars || !selected || !retained ||
        strcmp(retained->selection.instance,selected->selection.instance) ||
        !same_scope(source->scope,authority->scope) || !source->configured || !source->released || source->running ||
        !source_context(source,command))
        return fail(error,QA_ERROR_ARGUMENT,"Source Init requires its completed genuine configuration phase");
    source->declaration_owner=authority->declaration_owner;
    return refresh_source(manager,application,candidate,authority,error);
}
static bool retire(void *context,qa_application *application,
    const qa_application_startup_source *authority,qa_error *error)
{
    frontend_config_store *manager=context;
    if (authority && client_scope(authority->scope)) {
        if (manager->shared && frontend_config_store_source_pending(manager,application,manager->shared_candidate,authority))
            return fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement still retains its candidate shared settings owner");
        return frontend_remote_config_retire(manager->clients,application,authority,error);
    }
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_console *console=authority?authority->console:NULL; qa_cvars *cvars=authority?authority->cvars:NULL;
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!source) { variable_carries_discard(context,application,NULL,console); return true; }
    const qa_launch_instance *retained=instance(source);
    if (source->application!=application || source->cvars!=cvars || !selected || !retained ||
        retained->storage!=selected->storage || !same_scope(source->scope,authority->scope))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration retirement names another actual source owner");
    qa_frontend *frontend=source->manager->frontend;
    if (frontend_native_q3_count(frontend) &&
        !frontend_native_q3_retire_source(frontend,(qa_actor_owner)source->command.owner,selected,error)) return false;
    return frontend_config_store_retire(context,console,error);
}
static bool begin_retire(void *context,qa_application *application,
    const qa_application_startup_source *authority,qa_error *error)
{
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_actor_owner owner=authority?authority->scope.provider:0;
    qa_console *console=authority?authority->console:NULL; qa_cvars *cvars=authority?authority->cvars:NULL;
    frontend_config_store *manager=context;
    if (!manager || !application || !selected || !selected->storage || !owner)
        return fail(error,QA_ERROR_ARGUMENT,"Client lease retirement needs its actual entered source owner");
    frontend_config_source *source=frontend_config_store_source(manager,console);
    const qa_launch_instance *retained=source?instance(source):NULL;
    if (source && (source->application!=application || source->cvars!=cvars || !retained ||
        retained->storage!=selected->storage || source->command.owner!=owner || !same_scope(source->scope,authority->scope)))
        return fail(error,QA_ERROR_ARGUMENT,"Client lease retirement names another physical configuration source");
    if (manager->frontend->input_settings &&
        !frontend_input_settings_retire_source(manager->frontend->input_settings,application,console,error)) return false;
    /* A failed constructor can have no manager row. The native owner still
     * qualifies the exact metadata storage and never retires a same-name
     * published source's reader merely because its interned owner matches. */
    if (!frontend_selected_effects_retire_source(manager->frontend,owner,selected,error)) return false;
    if (source && authority->scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME &&
        !frontend_remote_configs_retire_staged_parent(manager->clients,application,authority,error)) return false;
    return !frontend_native_q3_count(manager->frontend) ||
        frontend_native_q3_retire_source(manager->frontend,owner,selected,error);
}
static qa_cvars *cvar_owner(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,const char *name)
{
    frontend_config_store *manager=context;
    if (frontend_config_store_client(manager,console))
        return frontend_config_store_cvar_owner(manager,console,command,name);
    frontend_config_source *source=frontend_config_store_source(context,console);
    return source && source->application==application?
        frontend_config_store_cvar_owner(context,console,command,name):NULL;
}
static bool cvar_edit(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,qa_cvars *registry,qa_cvars_edit **out,qa_error *error)
{ return frontend_config_store_cvar_edit(context,application,console,command,registry,out,error); }
static bool visible_cvars(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,size_t index,qa_cvars **out)
{
    frontend_config_store *manager=context;
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client && out) { *out=frontend_config_store_visible_cvars(manager,console,command,index); return true; }
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!out || !source || source->application!=application || !source_cvar_context(source,command)) return false;
    *out=frontend_config_store_visible_cvars(context,console,command,index); return true;
}
static bool source_read(void *context,qa_application *application,qa_console *console,
    const qa_command_context *command,const char *name,qa_bytes *bytes,void **lease,qa_error *error)
{
    frontend_config_store *manager=context;
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client) return frontend_remote_config_script_read(client,command,name,bytes,lease,error);
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (!source || source->application!=application)
        return fail(error,QA_ERROR_ARGUMENT,"Script read names another actual persistent source owner");
    return frontend_config_store_read(context,console,command,name,bytes,lease,error);
}
static void source_release(void *context,qa_application *application,qa_console *console,void *lease)
{
    frontend_config_store *manager=context;
    frontend_remote_config *client=frontend_config_store_client(manager,console);
    if (client) { frontend_remote_config_script_release(client,lease); return; }
    frontend_config_source *source=frontend_config_store_source(context,console);
    if (source && source->application==application) frontend_config_store_release(context,console,lease);
}
static void publish_input(config_seat *seat)
{
    qa_input_seat_context_publish(seat->publication_input,&seat->publication_command);
    qa_seat_console_context_publish(seat->publication_console,&seat->publication_command);
    qa_input_seat_configuration_publish(seat->publication_input,seat->input);
}
static void finish(void *context,qa_application *application,const qa_launch_snapshot *candidate,bool published)
{
    frontend_config_store *manager=context;
    frontend_remote_configs_finish(manager->clients,application,published?manager->prepared:candidate,published);
    variable_carries_discard(manager,application,published?manager->prepared:candidate,NULL);
    if (published) {
        if (manager->input_source) {
            for (size_t i=0;i<manager->input_count;++i) {
                config_seat *seat=manager->input_seats+i,*stable=manager->input_source->seats+i;
                publish_input(seat);
                frontend_authored_bindings *authored=stable->authored;
                stable->authored=seat->authored; seat->authored=authored;
                stable->movement_dialect=seat->movement_dialect;
            }
        }
        for (frontend_config_source *source=manager->sources;source;source=source->next) {
            if (source->application!=application || source->published || source->candidate!=manager->prepared) continue;
            for (size_t i=0;i<source->seat_count;++i) {
                config_seat *seat=source->seats+i;
                publish_input(seat);
                qa_input_seat_destroy(seat->input); seat->input=NULL;
                seat->publication_input=NULL;
                seat->publication_console=NULL;
            }
            source->published=true; source->candidate=NULL;
        }
        for (frontend_config_source *source=manager->sources;source;source=source->next)
            if (source->application==application && source->published) source->primary=source==manager->prepared_primary;
        if (manager->key_publication.owner) frontend_keys_publication_publish(&manager->key_publication);
        if (manager->storage && manager->shared_seeded) {
            manager->storage_seeded=true;
            manager->image_fov_touched|=manager->images_fov_touched;
            manager->sticky_seed_pending=false;
        }
        if (!manager->shared && !manager->images_program && !manager->images_console) {
            qa_cvar_archive_free(&manager->shared_archive);
            manager->images_source=(qa_application_startup_source){0};
            manager->root_console=NULL; manager->root_cvars=NULL; manager->root_command=(qa_command_context){0};
            manager->shared_seeded=manager->shared_archived=manager->images_audio_seeded=manager->images_fov_touched=false;
        }
    } else {
        frontend_keys_publication_discard(&manager->key_publication);
        /* The checked physical source retirement follows while its console is
         * still alive, including candidates which never reached preflight. */
        for (frontend_config_source *source=manager->sources;source;source=source->next)
            if (source->application==application && !source->published &&
                (!candidate || source->candidate==candidate))
                for (size_t i=0;i<source->seat_count;++i) {
                    source->seats[i].publication_input=NULL; source->seats[i].publication_console=NULL;
                }
    }
    discard_retained_input(manager);
    manager->prepared=NULL; manager->prepared_primary=NULL; manager->prepared_application=NULL;
}
static bool restore_source(void *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
static bool startup_source(void *context,qa_application *application,const qa_launch_snapshot *snapshot,
    const qa_application_startup_source *source,bool *primary,qa_error *error)
{
    frontend_config_store *manager=context;
    const qa_launch_instance *selected=source && source->descriptor && snapshot?
        qa_launch_snapshot_find(snapshot,source->descriptor->selection.instance):NULL;
    if (!manager || !application || !source || !primary || !selected ||
        selected->storage!=source->descriptor->storage || !source->scope.provider ||
        !source->console || !source->cvars || qa_console_cvars(source->console)!=source->cvars)
        return fail(error,QA_ERROR_ARGUMENT,"Startup route lost its actual physical configuration source");
    *primary=false;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(snapshot);
    const qa_launch_binding *binding=NULL;
    if (frontend_network_remote(manager->frontend)) {
        if (!choices || !choices->seat_count || choices->seats[0].bot ||
            source->scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME ||
            source->scope.seat!=choices->seats[0].id) return true;
        const qa_launch_seat *seat=choices->seats;
        if (seat->actor.generation) for (size_t i=0;i<choices->binding_count;++i) {
            const qa_launch_binding *candidate=choices->bindings+i;
            if (candidate->role==QA_ROLE_HUD && candidate->scope.kind==QA_SCOPE_ACTOR &&
                qa_actor_id_equal(candidate->scope.actor,seat->actor) && !*candidate->selector) {
                binding=candidate; break;
            }
        }
        if (!binding) binding=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=seat->id},QA_ROLE_HUD,"");
        if (!binding) binding=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_HUD,"");
    } else {
        if (!game_scope(source->scope)) return true;
        binding=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    }
    *primary=binding && !strcmp(binding->instance,selected->selection.instance);
    return true;
}
static bool retire_hosted(void *context,qa_application *application,const qa_application_startup_source *source,qa_error *error)
{
    frontend_config_store *manager=context;
    return manager && frontend_remote_config_retire_hosted(manager->clients,application,source,error);
}
static bool bind_hosted(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *target,const qa_application_startup_source *backing,qa_cvars **out,qa_error *error)
{
    frontend_config_store *manager=context;
    return manager && frontend_remote_config_bind_hosted(manager->clients,application,candidate,target,backing,out,error);
}
static void publish_hosted(void *context,qa_application *application,const qa_application_startup_source *source)
{
    frontend_config_store *manager=context;
    frontend_remote_config_publish_hosted(manager->clients,application,source);
}
static bool prepare_publication(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    void **out,qa_error *error)
{
    frontend_config_store *manager=context;
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    if (!owner || !out || *out || manager->publication)
        return fail(error,QA_ERROR_ARGUMENT,"Resource preparation requires its actual unconsumed shared owner");
    if (!frontend_config_store_images_release(manager,application,candidate,error)) return false;
    bool ok=frontend_shared_publication_prepare(owner,&manager->publication,error);
    *out=manager->publication; return ok;
}
static bool ready_publication(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    void *publication,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !publication || manager->publication!=publication ||
        !frontend_config_store_shared(manager,application,candidate))
        return fail(error,QA_ERROR_ARGUMENT,"Resource readiness lost its actual publication parent");
    return frontend_shared_publication_ready(manager->publication,error);
}
static bool owned_publication_ready(void *context,const qa_application *application,
    const qa_launch_snapshot *candidate,const void *publication)
{
    const frontend_config_store *manager=context;
    return manager && publication && manager->publication==publication &&
        manager->shared_application==application && manager->shared_candidate==candidate &&
        frontend_shared_publication_ready_is(manager->publication,manager->shared,
            manager->frontend,application,candidate);
}
static void consume_publication(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    void *publication)
{
    frontend_config_store *manager=context;
    if (owned_publication_ready(manager,application,candidate,publication))
        frontend_shared_publication_consume(manager->publication);
}
static bool finish_publication(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    void **publication,bool *complete,qa_error *error)
{
    frontend_config_store *manager=context;
    if (complete) *complete=false;
    if (!manager || !publication || !*publication || *publication!=manager->publication || !complete ||
        manager->shared_application!=application || manager->shared_candidate!=candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Resource cleanup lost its actual retained owner slots");
    bool ok=frontend_shared_publication_finish(&manager->shared,&manager->publication,complete,error);
    *publication=manager->publication;
    if (!manager->shared) { manager->shared_application=NULL; manager->shared_candidate=NULL; }
    return ok;
}
static bool abort_publication(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    void **publication,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !publication || *publication!=manager->publication ||
        manager->shared_application!=application || manager->shared_candidate!=candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Resource abort names another retained publication");
    bool ok=frontend_shared_publication_abort(&manager->publication,error);
    *publication=manager->publication; return ok;
}
static bool candidate_languages(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_language_ticket *const **out,size_t *count,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !out || !count)
        return fail(error,QA_ERROR_ARGUMENT,"Language readiness requires its actual shared owner");
    *out=NULL; *count=0;
    if (!manager->shared) return true;
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    return (owner && frontend_shared_publication_languages(owner,out,count)) ||
        fail(error,QA_ERROR_ARGUMENT,"Language readiness names another retained publication");
}
static bool candidate_values(void *context,const qa_application *application,const qa_launch_snapshot *candidate,
    const qa_cvars_edit **out,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !out || !application)
        return fail(error,QA_ERROR_ARGUMENT,"Candidate values require their retained actual parent");
    *out=NULL;
    if (!manager->shared) return true;
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    if (!owner) return fail(error,QA_ERROR_ARGUMENT,"Candidate values retain another or unavailable shared owner");
    *out=frontend_shared_values_prepared(frontend_shared_settings_values(owner));
    return *out || fail(error,QA_ERROR_ARGUMENT,"Candidate values lost their actual canonical edit");
}
static bool prepare_root(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    qa_console *console,qa_cvars *registry,const qa_command_context *command,qa_error *error)
{
    frontend_config_store *manager=context;
    qa_console *actual=NULL; qa_cvars *values=NULL; qa_command_context captured;
    if (!manager || manager->restoring || !console || !registry || !command || manager->shared ||
        manager->root_console || manager->images_console || manager->images_program ||
        manager->frontend->application!=application ||
        !qa_application_startup_root_phase(application,candidate) ||
        !qa_application_startup_root_read(application,candidate,&actual,&values,&captured,error) ||
        actual!=console || values!=registry || command->owner || command->session!=captured.session ||
        command->client!=captured.client || command->seat!=captured.seat || command->origin!=captured.origin ||
        command->dialect!=captured.dialect || command->registry!=captured.registry ||
        command->generation!=captured.generation || command->console_text!=captured.console_text ||
        command->script!=captured.script || command->direct!=captured.direct ||
        !qa_actor_id_equal(command->actor,captured.actor))
        return fail(error,QA_ERROR_ARGUMENT,"Shared root preparation lost its actual retained ENGINE authority");
    bool ok=frontend_shared_settings_begin_root(manager->frontend,manager,application,candidate,&manager->shared,error);
    if (manager->shared) {
        manager->shared_application=application; manager->shared_candidate=candidate;
        manager->root_console=console; manager->root_cvars=registry; manager->root_command=*command;
    }
    return ok && shared_storage_prepare(manager,error);
}
static bool advance_settings(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,bool validated,bool *complete,qa_error *error)
{
    if (!manager || !complete) return fail(error,QA_ERROR_ARGUMENT,"Candidate settings advancement lacks its owner");
    *complete=false;
    if (!manager->shared) { *complete=true; return true; }
    if (!prepare_retained_input(manager,application,candidate,error)) return false;
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    return owner?frontend_shared_settings_advance(owner,validated,complete,error):
        fail(error,QA_ERROR_ARGUMENT,"Candidate settings advancement names another shared preparation");
}
static bool candidate_retirement_ready(void *context,const qa_application *application,
    const qa_launch_snapshot *candidate,const qa_cvars_edit *edit,qa_error *error)
{
    frontend_config_store *manager=context;
    return (manager && manager->shared && manager->shared_application==application &&
        manager->shared_candidate==candidate &&
        frontend_shared_settings_retirement_ready(manager->shared,manager->frontend,application,candidate,edit)) ||
        fail(error,QA_ERROR_ARGUMENT,"Candidate retirement lacks its actual failed edit and complete physical history");
}
static bool advance_candidate(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    bool *complete,qa_error *error)
{ return advance_settings(context,application,candidate,false,complete,error); }
static bool advance_validated_candidate(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    bool *complete,qa_error *error)
{ return advance_settings(context,application,candidate,true,complete,error); }
static bool refresh_source(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *authority,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager || !authority || !authority->declaration_owner)
        return fail(error,QA_ERROR_ARGUMENT,"Shared refresh lacks its actual physical declaration owner");
    if (!manager->shared) return true;
    frontend_shared_settings *owner=frontend_config_store_shared(manager,application,candidate);
    if (!owner)
        return fail(error,QA_ERROR_ARGUMENT,"Shared refresh changed its retained candidate physical source");
    frontend_config_source *source=frontend_config_store_source(manager,authority->console);
    if (!frontend_config_store_source_pending(manager,application,candidate,authority)) {
        qa_application_startup_source held;
        frontend_remote_config *client=frontend_config_store_client(manager,authority->console);
        frontend_remote_config_view view;
        bool published=source?source->application==application && source->published && frontend_config_source_tuple(source,&held):
            frontend_remote_config_read(client,&view) && view.published && frontend_remote_config_tuple(client,&held);
        return (published && authority->descriptor && held.descriptor->storage==authority->descriptor->storage &&
            held.console==authority->console && held.cvars==authority->cvars && same_scope(held.scope,authority->scope)) ||
            fail(error,QA_ERROR_ARGUMENT,"Shared refresh names neither its pending source nor a genuine retained publication");
    }
    if (source) source->declaration_owner=authority->declaration_owner;
    else if (!frontend_remote_config_refresh(frontend_config_store_client(manager,authority->console),
        application,candidate,authority,error)) return false;
    return frontend_shared_settings_refresh(owner,authority,error);
}
static bool abort_candidate(void *context,qa_application *application,const qa_launch_snapshot *candidate,qa_error *error)
{
    frontend_config_store *manager=context;
    if (!manager) return fail(error,QA_ERROR_ARGUMENT,"Shared cancellation lacks its actual manager");
    if (manager->publication)
        return fail(error,QA_ERROR_ARGUMENT,"Candidate abort still retains actual resource children");
    if (!manager->shared) return true;
    if (manager->shared_application!=application || manager->shared_candidate!=candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Shared cancellation names another actual candidate");
    if (!shared_program_destroy(manager,error) || !frontend_shared_settings_abort(&manager->shared,error)) return false;
    manager->shared_application=NULL; manager->shared_candidate=NULL; return true;
}
frontend_config_store *frontend_config_store_create(qa_frontend *frontend,qa_error *error)
{
    if (!frontend) return fail(error,QA_ERROR_ARGUMENT,"Configuration manager needs its actual frontend owner"),NULL;
    frontend_config_store *manager=calloc(1,sizeof(*manager));
    if (!manager) return fail(error,QA_ERROR_MEMORY,"Retaining frontend configuration manager"),NULL;
    manager->frontend=frontend;
    manager->clients=frontend_remote_configs_create(frontend,manager,error);
    if (!manager->clients) { free(manager); return NULL; }
    manager->neutral=frontend_neutral_configs_create(frontend,manager,error);
    if (!manager->neutral) { frontend_remote_configs_destroy(manager->clients,NULL); free(manager); return NULL; }
    manager->hooks=(qa_application_startup_hooks){.context=manager,.prepare_source=prepare,.advance_source=advance,
        .read_script=phase_read,.release_script=phase_release,.script_complete=phase_complete,
        .allow_command=allow,.prepare_candidate=prepare_candidate,.release_source=phase_destroy,.finish_candidate=finish,
        .preinit_source=preinit,.restore_source=restore_source,.retire_source=retire,.cvar_owner=cvar_owner,.visible_cvars=visible_cvars,
        .read_source_script=source_read,.release_source_script=source_release,.begin_retire_source=begin_retire,
        .carry_source_variables=carry_variables,.configuration_store=configuration_store,.program_source=program_source,
        .parked_program_source_current=parked_program_current,
        .startup_source=startup_source,.retire_hosted_configuration=retire_hosted,
        .bind_hosted_configuration=bind_hosted,.publish_hosted_configuration=publish_hosted,
        .candidate_values=candidate_values,.prepare_root=prepare_root,.advance_images=advance_images,.advance_candidate=advance_candidate,
        .advance_validated_candidate=advance_validated_candidate,.refresh_source=refresh_source,
        .abort_candidate=abort_candidate,.cvar_edit=cvar_edit,.candidate_retirement_ready=candidate_retirement_ready,
        .candidate_languages=candidate_languages,.prepare_publication=prepare_publication,
        .ready_publication=ready_publication,.owned_publication_ready=owned_publication_ready,
        .consume_publication=consume_publication,.finish_publication=finish_publication,
        .abort_publication=abort_publication,.qw_logfrag_write=qw_log_write,.qw_logfrag_enabled=qw_log_enabled,
        .source_common_command=source_common_command,.source_files=source_files,.source_gamedir=source_gamedir};
    return manager;
}
const qa_application_startup_hooks *frontend_config_store_hooks(frontend_config_store *manager)
{ return manager?&manager->hooks:NULL; }
bool frontend_config_store_retired_ready(const frontend_config_store *manager,qa_error *error)
{
    return (manager && !manager->running && !manager->prepared && !manager->shared && !manager->publication && !manager->key_publication.owner &&
        !manager->sources && !manager->input_source && !manager->variable_carries && frontend_remote_configs_empty(manager->clients) &&
        frontend_neutral_configs_empty(manager->neutral)) ||
        fail(error,QA_ERROR_ARGUMENT,"Application callback context still retains actual configuration source owners");
}
bool frontend_config_store_restore_abort_unbound(frontend_config_store *manager,
    qa_application *application,qa_error *error)
{
    if (!manager) return true;
    bool pending=false;
    for (frontend_config_source *source=manager->sources;source;source=source->next)
        if (source->imported && !source->console && !source->cvars && !source->metadata) pending=true;
    if (!pending) return true;
    if (!manager->restoring || !application || manager->frontend->application!=application ||
        qa_application_get_state(application)!=QA_APPLICATION_STOPPING || manager->running ||
        manager->prepared || manager->shared || manager->publication || manager->key_publication.owner)
        return fail(error,QA_ERROR_ARGUMENT,"Unbound configuration import disposal requires its returned retired application");
    frontend_config_source **at=&manager->sources;
    while (*at) {
        frontend_config_source *source=*at;
        if (!source->imported || source->console || source->cvars || source->metadata) {
            at=&source->next; continue;
        }
        if (source->application!=application || !source->saved_instance || !*source->saved_instance ||
            source->candidate || source->running || source->phase || source->bindings ||
            source->admin_registered || source->write_registered || source->dump_registered || source->frag_registered)
            return fail(error,QA_ERROR_ARGUMENT,"Imported configuration row retains a genuine bound callback owner");
        frontend_config_source *next=source->next;
        if (!source_destroy(source,error)) return false;
        *at=next;
    }
    return true;
}
bool frontend_config_store_destroy(frontend_config_store *manager,qa_error *error)
{
    if (!manager) return true;
    if (manager->running || manager->prepared || manager->shared || manager->publication ||
        manager->key_publication.owner || manager->input_source)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration manager retains an executing or prepared candidate");
    if (!frontend_qw_logfile_close(&manager->qw_logfile,error)) return false;
    if (!shared_program_destroy(manager,error)) return false;
    if (!frontend_neutral_configs_destroy(manager->neutral,error)) return false;
    manager->neutral=NULL;
    if (!frontend_remote_configs_destroy(manager->clients,error)) return false;
    manager->clients=NULL;
    while (manager->sources) {
        frontend_config_source *source=manager->sources; frontend_config_source *next=source->next;
        if (!source_destroy(source,error)) return false;
        manager->sources=next;
    }
    if (!frontend_source_admin_destroy(manager->admin,error)) return false;
    manager->admin=NULL;
    free(manager->admin_source);
    variable_carries_discard(manager,NULL,NULL,NULL);
    frontend_shared_storage_destroy(manager->storage);
    qa_buffer_free(&manager->restored_storage);
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
    if (!manager || manager->running || manager->shared)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration archive requires its returned published source owners");
    for (frontend_config_source *source=manager->sources;source;source=source->next) if (source->published && source->primary) {
        qa_command_context command;
        if (!current_command(source,&command,error)) return false;
        const qa_launch_instance *selected=instance(source);
        const qa_product *product=qa_catalog_product(frontend_config_files_catalog(source->files),
            frontend_config_files_product(source->files));
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
            char index[16],path[64]; snprintf(index,sizeof(index),"%" PRIu32,seat->logical);
            snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)seat->logical+1);
            const char *client_owner[4]={"client",product->key,selected->selection.implementation,index};
            const char *mouse_owner[3]={"input",dialects[source->command.dialect],index};
            qa_input_seat *active=manager->frontend->seats && i<manager->frontend->options.seats?
                manager->frontend->seats[i].input:NULL;
            if (!active) return fail(error,QA_ERROR_ARGUMENT,"Source input archive lost its actual published seat");
            size_t count=qa_input_seat_binding_count(active);
            if (count>SIZE_MAX/sizeof(qa_input_binding)) return fail(error,QA_ERROR_MEMORY,"Source input archive exceeds binding storage");
            qa_input_binding *bindings=count?malloc(count*sizeof(*bindings)):NULL;
            if (count && !bindings) return fail(error,QA_ERROR_MEMORY,"Retaining actual live input bindings for archive");
            for (size_t n=0;n<count;++n) {
                bindings[n]=*qa_input_seat_binding_at(active,n);
                if (bindings[n].input.kind==QA_PHYSICAL_BUTTON || bindings[n].input.kind==QA_PHYSICAL_AXIS)
                    bindings[n].input.device=0;
            }
            qa_console_history *history=qa_seat_console_history(manager->frontend->seats[i].console);
            size_t history_count=qa_console_history_count(history);
            char **lines=history_count<=SIZE_MAX/sizeof(*lines)?malloc(history_count?history_count*sizeof(*lines):1):NULL;
            if (!lines) { free(bindings); return fail(error,QA_ERROR_MEMORY,"Retaining actual console history for settings archive"); }
            for (size_t n=0;n<history_count;++n) lines[n]=(char *)qa_console_history_at(history,n);
            qa_input_command_tuning tuning;
            bool ok=qa_input_settings_read(seat->mouse,(qa_movement_kind)seat->movement_dialect,&tuning,error);
            qa_seat_settings settings=seat->settings;
            if (ok) {
                settings.bindings=bindings; settings.binding_count=count;
                settings.mouse=tuning.mouse; settings.gamepad=*qa_input_seat_gamepad_tuning(active);
                settings.has_always_run=true; settings.always_run=tuning.view.always_run;
                settings.history=lines; settings.history_count=history_count;
                qa_haptic_player *haptics=manager->frontend->input?qa_input_platform_haptics(manager->frontend->input,(unsigned)i):NULL;
                if (haptics) { settings.rumble=haptics->enabled; settings.rumble_strength=haptics->strength; }
                if (manager->frontend->input && !qa_input_platform_selection(manager->frontend->input,(unsigned)i,&settings.controller))
                    ok=fail(error,QA_ERROR_ARGUMENT,"Input archive lost its actual configured controller selection");
                ok=ok && qa_settings_save_cvars(store,client_owner,4,seat->cvars,error) &&
                    qa_settings_save_cvars(input,mouse_owner,3,seat->mouse,error) &&
                    qa_settings_save_seat(input,path,&settings,error);
            }
            free(lines); free(bindings); if (!ok) return false;
        }
    }
    if (!frontend_remote_configs_save(manager->clients,manager->frontend->application,error)) return false;
    if (!frontend_neutral_configs_save(manager->neutral,error)) return false;
    if (manager->storage) {
        qa_cvars *engine=qa_application_cvars(manager->frontend->application);
        if (!manager->storage_seeded || !shared_storage_current(manager) || !qa_cvars_observer_idle(engine))
            return fail(error,QA_ERROR_ARGUMENT,"Shared archive lost its published ENGINE and retained store authorities");
        if ((!manager->frontend->options.dedicated && !frontend_shared_storage_save_images(manager->storage,engine,error)) ||
            !frontend_shared_storage_save_input(manager->storage,engine,error)) return false;
        qa_settings_store sticky={0};
        if (!frontend_shared_storage_input(manager->storage,&sticky))
            return fail(error,QA_ERROR_ARGUMENT,"Shared save lost its admitted optional sticky preference store");
        if (!manager->frontend->options.dedicated && sticky.vfs && !manager->sticky_seed_pending) {
            qa_frontend *f=manager->frontend;
            float effects=0,music=0;
            bool shuffle=false; const char *menu=NULL;
            if (!qa_audio_engine_gains_read(f->audio,&effects,&music) ||
                !frontend_music_sources_parent_is(f->music_sources,f,f->audio) ||
                !frontend_music_sources_preferences_read(f->music_sources,&shuffle,&menu))
                return fail(error,QA_ERROR_ARGUMENT,"Audio save lost its actual published gains and music preferences");
            qa_audio_device_options output=f->device?qa_audio_device_requested_configuration(f->device):
                (qa_audio_device_options){.format=f->audio_output_format};
            frontend_shared_audio_preferences audio={.format=output.format,.device=output.name,
                .effects=effects,.music=music,.present=true,.has_shuffle=true,.shuffle=shuffle,
                .has_menu_track=true,.menu_track=menu};
            if (!frontend_shared_storage_save_audio(manager->storage,&audio,error)) return false;
            frontend_shared_view_preferences view;
            if (!frontend_view_settings_parent_is(manager->frontend->view_settings,manager->frontend,engine) ||
                !frontend_view_settings_preferences(manager->frontend->view_settings,&view))
                return fail(error,QA_ERROR_ARGUMENT,"View save lost its installed canonical preference owner");
            if (!frontend_shared_storage_save_view(manager->storage,&view,error)) return false;
        }
    }
    return true;
}
bool frontend_config_store_retire(frontend_config_store *manager,const qa_console *console,qa_error *error)
{
    if (!manager || manager->running) return fail(error,QA_ERROR_ARGUMENT,"Configuration retirement requires returned source callbacks");
    frontend_config_source **at=&manager->sources;
    while (*at && (*at)->console!=console) at=&(*at)->next;
    if (!*at) { variable_carries_discard(manager,NULL,NULL,console); return true; }
    frontend_config_source *source=*at,*next=source->next;
    if (manager->shared && source->application==manager->shared_application && source->candidate==manager->shared_candidate)
        return fail(error,QA_ERROR_ARGUMENT,"Source retirement still retains its candidate shared settings owner");
    if (!source_destroy(source,error)) return false;
    variable_carries_discard(manager,NULL,NULL,console);
    *at=next; return true;
}
void frontend_config_store_rebind(frontend_config_store *manager,qa_frontend *frontend)
{ if (manager && !manager->running && !manager->shared) { manager->frontend=frontend; frontend_source_admin_rebind(manager->admin,frontend); frontend_remote_configs_rebind(manager->clients,frontend,manager); frontend_neutral_configs_rebind(manager->neutral,frontend,manager); } }

bool frontend_config_store_neutral_options(frontend_config_store *manager,uint32_t physical,
    qa_movement_kind movement,frontend_client_source_options *out,qa_error *error)
{
    if (!manager || manager->running || manager->prepared || manager->shared)
        return fail(error,QA_ERROR_ARGUMENT,"Neutral construction requires its returned configuration manager");
    return frontend_neutral_config_options(manager->neutral,physical,movement,out,error);
}
bool frontend_config_store_neutral_read(const frontend_config_store *manager,const qa_console *console,
    frontend_neutral_config_view *out,qa_error *error)
{
    return manager && frontend_neutral_config_read(manager->neutral,console,out,error);
}
bool frontend_config_store_neutral_retirement_release_ready(const frontend_config_store *manager,
    const qa_input_seat *input,const qa_input_release *release,qa_error *error)
{
    return manager?frontend_neutral_config_retirement_release_ready(manager->neutral,input,release,error):
        fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement requires its retained configuration manager");
}
bool frontend_config_store_neutral_checkpoint_read(const frontend_config_store *manager,const qa_console *console,
    frontend_neutral_config_view *out,qa_error *error)
{
    return manager && frontend_neutral_config_checkpoint_read(manager->neutral,console,out,error);
}
bool frontend_config_store_neutral_retired_recipient(const frontend_config_store *manager,
    const qa_application_client_source *source,bool *retained,qa_error *error)
{
    return manager && frontend_neutral_config_retired_recipient(manager->neutral,source,retained,error);
}
bool frontend_config_store_neutral_startup_read(const frontend_config_store *manager,
    qa_application_client_source *out,bool *found,qa_error *error)
{
    return manager && frontend_neutral_config_startup_read(manager->neutral,out,found,error);
}
qa_application_client_preparation *frontend_config_store_client_preparation(const frontend_config_store *manager)
{
    qa_application_client_preparation *client=manager?frontend_shared_settings_client(manager->shared):NULL;
    return manager && client && manager->shared_application==manager->frontend->application &&
        !manager->shared_candidate && qa_application_client_prepare_associated(manager->shared_application,client)?client:NULL;
}
bool frontend_config_store_client_input_configuration(const frontend_config_store *manager,
    const qa_application_client_preparation *preparation,uint32_t ordinal,qa_input_seat **out,qa_error *error)
{
    return manager && frontend_neutral_config_client_input(manager->neutral,preparation,ordinal,out,error);
}
bool frontend_config_store_client_controller_selection(const frontend_config_store *manager,
    const qa_application_client_preparation *preparation,uint32_t ordinal,qa_controller_selection *out,qa_error *error)
{
    return manager && frontend_neutral_config_client_controller(manager->neutral,preparation,ordinal,out,error);
}
bool frontend_config_store_client_view_transition(const frontend_config_store *manager,
    const qa_application_client_preparation *preparation,frontend_view_transition *out,qa_error *error)
{
    const qa_frontend *f=manager?manager->frontend:NULL;
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    bool published=false;
    qa_input_seat *input=NULL;
    if (!f || !out || !source ||
        !qa_application_client_prepare_phase_is(preparation,QA_CLIENT_PREPARE_RESOURCES) ||
        !frontend_neutral_config_client_input(manager->neutral,preparation,source->context.physical_seat,&input,error) ||
        !frontend_view_settings_parent_is(f->view_settings,f,qa_application_cvars(f->application)) ||
        !frontend_view_settings_has_published(f->view_settings,&published))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT view transition lost its actual token and published preference owner");
    *out=published?FRONTEND_VIEW_REPLACEMENT:FRONTEND_VIEW_INITIAL; return true;
}
static bool client_settings_current(const frontend_config_store *manager,const qa_application_client_preparation *client)
{
    return manager && manager->shared && manager->shared_application==manager->frontend->application &&
        !manager->shared_candidate && frontend_shared_settings_client(manager->shared)==client &&
        qa_application_client_prepare_associated(manager->shared_application,client);
}
bool frontend_config_store_client_settings_begin(frontend_config_store *manager,
    qa_application_client_preparation *client,qa_error *error)
{
    if (!manager || manager->shared || manager->publication || manager->restoring || manager->running ||
        !qa_application_client_prepare_associated(manager->frontend->application,client))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT settings already retain another canonical preparation");
    bool ok=frontend_shared_settings_begin_client(manager->frontend,manager,manager->frontend->application,
        client,&manager->shared,error);
    if (manager->shared) { manager->shared_application=manager->frontend->application; manager->shared_candidate=NULL; }
    return ok;
}
bool frontend_config_store_client_settings_advance(frontend_config_store *manager,
    qa_application_client_preparation *client,bool validated,bool *complete,qa_error *error)
{
    return client_settings_current(manager,client)?frontend_shared_settings_advance(manager->shared,validated,complete,error):
        fail(error,QA_ERROR_ARGUMENT,"CLIENT release lost its actual canonical settings owner");
}
bool frontend_config_store_client_settings_seed(frontend_config_store *manager,
    qa_application_client_preparation *client,qa_cvar_archive *snapshot,qa_error *error)
{
    if (!snapshot || !client_settings_current(manager,client) || !shared_storage_current(manager) ||
        !qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_CONFIGURATION))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT archive seed lost its actual configuration and retained stores");
    qa_cvar_archive devices={0};
    if (!frontend_shared_storage_load_devices(manager->storage,&devices,error)) return false;
    bool ok=true;
    for (size_t i=0;ok && i<devices.count;++i)
        ok=archive_remember(snapshot,devices.entries[i].name,devices.entries[i].value,error);
    qa_cvar_archive_free(&devices);
    if (!ok || !manager->sticky_seed_pending || manager->frontend->options.dedicated) return ok;
    frontend_shared_values *values=frontend_shared_settings_values(manager->shared);
    if (!images_output(manager,values,error)) return false;
    const qa_cvar_archive *archive=frontend_shared_storage_archive(manager->storage);
    for (size_t i=0;archive && i<archive->count;++i) {
        const qa_cvar_archive_entry *row=archive->entries+i;
        if (strcmp(row->name,"s_outputRate") && strcmp(row->name,"s_outputBits") &&
            strcmp(row->name,"s_outputChannels") && strcmp(row->name,"volume") &&
            strcmp(row->name,"bgmvolume") && strcmp(row->name,"music_shuffle") && strcmp(row->name,"music_menu_track")) continue;
        if (!archive_remember(snapshot,row->name,row->value,error)) return false;
    }
    const frontend_shared_view_preferences *view=frontend_shared_storage_view(manager->storage);
    if (!manager->image_fov_touched && view && view->present) {
        char text[64];
        if (!qa_format_ecmascript_number(view->field_of_view,text,error) || !archive_remember(snapshot,"fov",text,error)) return false;
        qa_cvar_archive_entry row={(char *)"fov",text}; qa_cvar_archive one={.entries=&row,.count=1};
        if (!frontend_shared_values_archive(values,&one,error)) return false;
    }
    return true;
}
bool frontend_config_store_client_settings_prepare(frontend_config_store *manager,
    qa_application_client_preparation *client,qa_error *error)
{
    if (!client_settings_current(manager,client) ||
        !qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_RESOURCES))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT resources lost their actual entered preparation");
    if (!manager->publication && !frontend_shared_publication_prepare(manager->shared,&manager->publication,error)) return false;
    return frontend_shared_publication_ready(manager->publication,error);
}
bool frontend_config_store_client_settings_ready_is(const frontend_config_store *manager,
    const qa_application_client_preparation *client)
{
    return client_settings_current(manager,client) && frontend_shared_publication_ready_is(manager->publication,
        manager->shared,manager->frontend,manager->shared_application,NULL);
}
void frontend_config_store_client_settings_consume(frontend_config_store *manager,
    qa_application_client_preparation *client)
{
    if (frontend_config_store_client_settings_ready_is(manager,client)) frontend_shared_publication_consume(manager->publication);
}
bool frontend_config_store_client_settings_finish(frontend_config_store *manager,
    qa_application_client_preparation *client,bool *complete,qa_error *error)
{
    if (!complete || !client_settings_current(manager,client))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT cleanup lost its consumed canonical publication");
    bool ok=frontend_shared_publication_finish(&manager->shared,&manager->publication,complete,error);
    if (*complete && !manager->shared && !manager->publication) {
        manager->shared_application=NULL; manager->shared_candidate=NULL; manager->sticky_seed_pending=false;
    }
    return ok;
}
bool frontend_config_store_client_settings_abort(frontend_config_store *manager,
    qa_application_client_preparation *client,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !manager || !client || !qa_application_client_prepare_associated(manager->frontend->application,client))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT abort lost its actual application phase");
    if (!manager->shared) { *complete=!manager->publication; return *complete; }
    if (!client_settings_current(manager,client)) return fail(error,QA_ERROR_ARGUMENT,"CLIENT abort names another scalar owner");
    bool released=false;
    qa_error original={0};
    bool ok=frontend_shared_settings_cancel_advance(manager->shared,&released,&original);
    if (ok && !released) return true;
    if (ok) ok=frontend_shared_settings_abort(&manager->shared,&original);
    if (!ok && manager->shared) {
        qa_error cleanup={0}; bool terminal=false;
        bool cleaned=frontend_shared_settings_client_shutdown(&manager->shared,&terminal,&cleanup);
        if (original.code==QA_OK && cleanup.code!=QA_OK) original=cleanup;
        ok=cleaned && ok;
    }
    if (!manager->shared) {
        manager->publication=NULL; manager->shared_application=NULL; manager->shared_candidate=NULL;
        *complete=true;
    }
    if (!ok && error) *error=original;
    return ok;
}
bool frontend_config_store_client_settings_cancel(frontend_config_store *manager,
    qa_application_client_preparation *client,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !client_settings_current(manager,client) ||
        !qa_application_client_prepare_cancel_entered(client))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT release cancellation lost its exact retained settings owner");
    return frontend_shared_settings_cancel_advance(manager->shared,complete,error);
}
bool frontend_config_store_neutral_pending_options(frontend_config_store *manager,uint32_t physical,
    frontend_client_source_options *out,qa_error *error)
{
    if (!manager || manager->running || manager->prepared || manager->shared)
        return fail(error,QA_ERROR_ARGUMENT,"Neutral construction requires its returned configuration manager");
    return frontend_neutral_config_pending_options(manager->neutral,physical,out,error);
}
bool frontend_config_store_neutral_movement_adopt(frontend_config_store *manager,const qa_console *console,
    qa_movement_kind movement,qa_error *error)
{
    return manager && frontend_neutral_config_movement_adopt(manager->neutral,console,movement,error);
}
bool frontend_config_store_neutral_options_cancel(frontend_config_store *manager,
    frontend_client_source_options *options,qa_error *error)
{
    return manager && frontend_neutral_config_options_cancel(manager->neutral,options,error);
}
bool frontend_config_store_client_profile(const frontend_config_store *manager,qa_game_family family,
    qa_product_id *out,qa_error *error)
{
    if (!manager || !out || !manager->frontend || !manager->frontend->application)
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT profile needs its retained frontend selection");
    if (family!=QA_GAME_Q1 && family!=QA_GAME_Q2 && family!=QA_GAME_Q3)
        return fail(error,QA_ERROR_UNSUPPORTED,"Neutral CLIENT configuration requires a genuine Quake profile");
    qa_frontend *f=manager->frontend;
    const qa_product *product=frontend_product_selection(qa_application_catalog(f->application),f->options.game);
    if (!product || product->family!=family || !product->builtin || product->program_kind!=QA_PROGRAM_BUILTIN ||
        product->availability!=QA_CONTENT_INSTALLED)
        return fail(error,QA_ERROR_ARGUMENT,"Remote CLIENT needs an explicitly selected installed compiled profile");
    *out=product->id; return true;
}
bool frontend_config_store_neutral_adopt_store(frontend_config_store *manager,const qa_launch_instance *selected,
    frontend_config_files *files,qa_error *error)
{
    if (!manager || manager->shared || manager->running || manager->restoring || !selected || !files ||
        qa_launch_instance_catalog(selected)!=frontend_config_files_catalog(files) ||
        selected->selection.product!=frontend_config_files_product(files) ||
        !frontend_config_files_global_current(files,
            frontend_global_settings_storage_user_store(manager->frontend->global_settings_storage),
            frontend_global_settings_storage_device_store(manager->frontend->global_settings_storage)))
        return fail(error,QA_ERROR_ARGUMENT,"Neutral input store requires its actual retained product and global authorities");
    qa_frontend *f=manager->frontend;
    return (f->input_config || frontend_input_profile_bind_store(f,frontend_config_files_catalog(files),
        frontend_config_files_product(files),frontend_config_files_store(files,false),error)) &&
        shared_storage_prepare(manager,error);
}

static bool config_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX;
    if (!qa_source_save_count(io,&count,maximum)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if (count>io->input.size-io->offset) {
        io->failed=true;
        return fail(io->error,QA_ERROR_FORMAT,"Configuration blob exceeds its admitted section");
    }
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool registry_bytes(frontend_config_source *source,qa_source_save_io *io,qa_cvars **cvars)
{
    uint32_t dialect=*cvars?qa_cvars_dialect(*cvars):0;
    qa_buffer captured={0}; qa_bytes bytes={0};
    bool ok=qa_source_save_u32(io,&dialect) && dialect<=QA_CONSOLE_Q3;
    if (ok && io->direction==QA_SOURCE_SAVE_WRITE) {
        ok=*cvars && qa_cvars_save_capture(*cvars,&captured,io->error);
        bytes=(qa_bytes){captured.data,captured.size};
    }
    if (ok) ok=config_blob(io,&bytes) && bytes.size;
    if (ok && io->direction==QA_SOURCE_SAVE_READ) {
        *cvars=registry(source,(qa_console_dialect)dialect,io->error);
        qa_cvars_restore *ticket=NULL;
        ok=*cvars && qa_cvars_save_prepare(*cvars,bytes,&ticket,io->error) && qa_cvars_save_commit(ticket,io->error);
        if (!ok) qa_cvars_save_abort(ticket);
    }
    qa_buffer_free(&captured); return ok;
}
bool frontend_config_store_visit(const frontend_config_store *manager,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!manager || manager->restoring || manager->running || manager->prepared || manager->shared || manager->variable_carries || !visitor ||
        !visitor->pool || !visitor->catalog || !visitor->view)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration inventory requires returned published owners");
    for (const frontend_config_source *source=manager->sources;source;source=source->next) {
        const qa_launch_instance *selected=instance(source);
        qa_catalog *catalog=selected?qa_launch_instance_catalog(selected):NULL;
        if (!source->published || source->imported || source->phase || source->running || !selected ||
            !catalog || !frontend_config_files_visit(source->files,visitor,error) ||
            !visitor->pool(visitor->context,qa_catalog_resources(catalog),error) ||
            !visitor->catalog(visitor->context,catalog,error) ||
            !visitor->view(visitor->context,qa_catalog_files(catalog),error) ||
            !selected->content || !visitor->pool(visitor->context,qa_vfs_resources(selected->content),error) ||
            !visitor->view(visitor->context,selected->content,error)) return false;
    }
    return frontend_remote_configs_visit(manager->clients,visitor,error) &&
        frontend_neutral_configs_visit(manager->neutral,visitor,error) &&
        frontend_qw_logfile_visit(manager->qw_logfile,visitor,error) &&
        (!manager->storage || (manager->storage_seeded && shared_storage_current(manager) &&
            frontend_shared_storage_visit(manager->storage,visitor,error)));
}
static bool config_header(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','C','S'};
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFCS",4) &&
        qa_source_save_count(io,count,
            io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX);
}
static bool config_row(frontend_config_source *source,qa_source_save_io *io,
    qa_application_content_graph *graph,frontend_keys *keys,const frontend_keys_cvar_refs *refs)
{
    (void)refs;
    const bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    char *name=writing?(char *)instance(source)->selection.instance:NULL;
    qa_sha256_digest identity=writing?instance(source)->identity:(qa_sha256_digest){0};
    uint32_t dialect=source->command.dialect,movement=source->movement_dialect;
    uint32_t scope=source->scope.kind;
    uint64_t key=frontend_key_profile_id(source->keys);
    bool same=source->fallback==source->movement;
    bool ok=frontend_save_text(io,&name) && name && *name &&
        qa_source_save_bytes(io,&identity,sizeof(identity)) && qa_source_save_u32(io,&dialect) && dialect<=QA_CONSOLE_Q3 &&
        qa_source_save_u32(io,&scope) && scope<=QA_APPLICATION_CONSOLE_Q2_GAME &&
        qa_source_save_u32(io,&source->scope.seat) &&
        qa_source_save_u32(io,&movement) && movement<=QA_CONSOLE_Q3 &&
        qa_source_save_bool(io,&source->primary) && qa_source_save_bool(io,&source->has_mod) &&
        qa_source_save_u64(io,&key);
    if (!writing) { source->saved_instance=name; source->saved_identity=identity;
        source->command.dialect=(qa_console_dialect)dialect; source->movement_dialect=(qa_console_dialect)movement;
        source->scope.kind=(qa_application_console_kind)scope; }
    if (ok && !game_scope(source->scope)) ok=fail(io->error,QA_ERROR_FORMAT,"Saved configuration has no actual GAME scope");
    qa_buffer captured={0}; qa_bytes bytes={0};
    if (ok && key) {
        if (!writing) {
            source->keys=frontend_keys_profile(keys,key);
            ok=source->keys && frontend_key_profile_saved_instance(source->keys) &&
                !strcmp(frontend_key_profile_saved_instance(source->keys),name) &&
                frontend_key_profile_retain(source->keys,io->error);
            if (ok) source->files=frontend_key_profile_files(source->keys);
            else source->keys=NULL;
        }
        ok=ok && dialect==QA_CONSOLE_Q3;
    } else if (ok) {
        if (writing) { ok=frontend_config_files_checkpoint(source->files,graph,&captured,io->error);
            bytes=(qa_bytes){captured.data,captured.size}; }
        if (ok) ok=config_blob(io,&bytes) && bytes.size;
        if (ok && !writing) ok=frontend_config_files_restore(graph,bytes,&source->files,io->error);
        qa_buffer_free(&captured);
    }
    if (ok) ok=registry_bytes(source,io,&source->movement) && qa_source_save_bool(io,&same);
    if (ok && same && !writing) source->fallback=source->movement;
    else if (ok && !same) ok=registry_bytes(source,io,&source->fallback);
    if (ok) ok=qa_cvars_dialect(source->movement)==movement && qa_cvars_dialect(source->fallback)==dialect &&
        same==(movement==dialect);
    if (ok) ok=qa_source_save_count(io,&source->seat_count,QA_INPUT_LOCAL_SEATS);
    for (size_t i=0;ok && i<source->seat_count;++i) {
        config_seat *seat=source->seats+i;
        uint32_t seat_movement=seat->movement_dialect;
        ok=qa_source_save_u32(io,&seat->logical) && qa_source_save_u32(io,&seat_movement) &&
            seat_movement<=QA_CONSOLE_Q3 && qa_source_save_bool(io,&seat->cvars_transferred);
        if (!writing) seat->movement_dialect=(qa_console_dialect)seat_movement;
        if (ok && !writing) { seat->authored=frontend_authored_bindings_create(io->error); ok=seat->authored!=NULL; }
        if (ok) ok=frontend_authored_bindings_fields(seat->authored,io) && frontend_authored_bindings_completed(seat->authored);
        for (size_t previous=0;ok && previous<i;++previous) ok=source->seats[previous].logical!=seat->logical;
        if (ok && seat->cvars_transferred) {
            char *receiver=NULL;
            uint32_t logical=seat->logical;
            if (writing) {
                const qa_launch_instance *physical=NULL;
                const frontend_client_registry *owner=frontend_client_registry_lookup(source->manager->frontend,seat->cvars);
                ok=owner && owner==seat->registry &&
                    frontend_client_registry_source(owner,&physical,&logical) &&
                    frontend_client_registry_matches(owner,instance(source),seat->logical);
                if (ok) receiver=(char *)physical->selection.instance;
            }
            if (ok) ok=frontend_save_text(io,&receiver) && receiver && *receiver &&
                qa_source_save_u32(io,&logical) && logical==seat->logical && !strcmp(receiver,name);
            if (!writing) seat->registry_instance=receiver;
        } else if (ok) ok=registry_bytes(source,io,&seat->cvars);
        if (ok) ok=registry_bytes(source,io,&seat->mouse) && qa_source_save_bool(io,&seat->found);
        if (ok) ok=qa_cvars_dialect(seat->mouse)==dialect &&
            (seat->cvars_transferred || qa_cvars_dialect(seat->cvars)==dialect);
        if (ok && seat->found) {
            if (writing) { ok=qa_seat_settings_encode(&seat->settings,&captured,io->error);
                bytes=(qa_bytes){captured.data,captured.size}; }
            if (ok) ok=config_blob(io,&bytes) && bytes.size;
            if (ok && !writing) ok=qa_seat_settings_parse(bytes,&seat->settings,io->error);
            qa_buffer_free(&captured);
        }
    }
    bool dedicated=source->dedicated_bindings!=NULL;
    if (ok) ok=qa_source_save_bool(io,&dedicated) && dedicated==(source->primary && !source->seat_count);
    if (ok && dedicated) {
        if (!writing) source->dedicated_bindings=frontend_config_bindings_create(io->error);
        ok=source->dedicated_bindings && frontend_config_bindings_fields(source->dedicated_bindings,io);
    }
    return ok;
}
bool frontend_config_store_checkpoint(const frontend_config_store *manager,
    const qa_application_content_graph *graph,const frontend_keys_cvar_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!manager || manager->restoring || manager->running || manager->prepared || manager->shared || manager->variable_carries ||
        manager->images_console || manager->images_program || manager->restored_storage.data || !graph ||
        !out || out->data || out->size) return fail(error,QA_ERROR_ARGUMENT,"Configuration capture requires returned actual owners");
    size_t count=0;
    for (const frontend_config_source *source=manager->sources;source;source=source->next) {
        if (!source->published || source->phase || source->running || source->imported || !source->metadata)
            return fail(error,QA_ERROR_ARGUMENT,"Unpublished source configuration cannot enter a continuation");
        if (source->keys && frontend_key_profile_registry(source->keys)!=source->cvars)
            return fail(error,QA_ERROR_ARGUMENT,"Configuration key alias leaves its genuine physical GAME registry");
        ++count;
    }
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && config_header(&io,&count);
    for (frontend_config_source *source=manager->sources;ok && source;source=source->next)
        ok=config_row(source,&io,(qa_application_content_graph *)graph,NULL,refs);
    qa_buffer clients={0}; qa_bytes bytes={0};
    if (ok) { ok=frontend_remote_configs_checkpoint(manager->clients,graph,&clients,error);
        bytes=(qa_bytes){clients.data,clients.size}; }
    if (ok) ok=config_blob(&io,&bytes);
    qa_buffer_free(&clients);
    qa_buffer neutral={0};
    if (ok) { ok=frontend_neutral_configs_checkpoint(manager->neutral,graph,&neutral,error);
        bytes=(qa_bytes){neutral.data,neutral.size}; }
    if (ok) ok=config_blob(&io,&bytes);
    qa_buffer_free(&neutral);
    bool shared=manager->storage!=NULL;
    if (ok) ok=(shared || (!count && frontend_remote_configs_empty(manager->clients) && frontend_neutral_configs_empty(manager->neutral))) &&
        (!shared || (manager->storage_seeded && shared_storage_current(manager))) && qa_source_save_bool(&io,&shared);
    qa_buffer storage={0};
    if (ok && shared) {
        ok=frontend_shared_storage_checkpoint(manager->storage,graph,&storage,error);
        bytes=(qa_bytes){storage.data,storage.size};
        if (ok) ok=config_blob(&io,&bytes);
    }
    qa_buffer_free(&storage);
    bool touched=manager->image_fov_touched,sticky=manager->sticky_seed_pending;
    if (ok) ok=(!touched || shared) && (!sticky || (shared && !count &&
        frontend_remote_configs_empty(manager->clients))) && qa_source_save_bool(&io,&touched) &&
        qa_source_save_bool(&io,&sticky);
    bool early=manager->admin!=NULL;
    if (ok) ok=qa_source_save_bool(&io,&early);
    if (ok && early) {
        const qa_console *console=frontend_source_admin_console(manager->admin);
        frontend_config_source *source=console?frontend_config_store_source((frontend_config_store *)manager,console):NULL;
        qa_buffer admin={0};
        char *name=source?(char *)instance(source)->selection.instance:NULL;
        qa_sha256_digest identity=source?instance(source)->identity:(qa_sha256_digest){0};
        qa_settings_store user=frontend_global_settings_storage_user_store(manager->frontend->global_settings_storage);
        ok=source && source==published_primary(manager,manager->frontend->application) &&
            frontend_source_admin_checkpoint(manager->admin,source->application,source->console,source->cvars,
                &source->command,user,&admin,error) && frontend_save_text(&io,&name) &&
            qa_source_save_bytes(&io,&identity,sizeof(identity));
        bytes=(qa_bytes){admin.data,admin.size};
        if (ok) ok=config_blob(&io,&bytes);
        qa_buffer_free(&admin);
        if (!ok && error && error->code==QA_OK)
            fail(error,QA_ERROR_ARGUMENT,"Early administration capture lacks its genuine published primary Source");
    }
    bool logging=manager->qw_logfile!=NULL;
    if (ok) ok=qa_source_save_bool(&io,&logging);
    if (ok && logging) {
        qa_buffer logfile={0};
        ok=frontend_qw_logfile_checkpoint(manager->qw_logfile,graph,&logfile,error);
        bytes=(qa_bytes){logfile.data,logfile.size};
        if (ok) ok=config_blob(&io,&bytes);
        qa_buffer_free(&logfile);
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_config_store_restore(qa_frontend *frontend,qa_application *application,
    qa_application_content_graph *graph,frontend_keys *keys,const frontend_keys_cvar_refs *refs,
    qa_bytes bytes,frontend_config_store **out,qa_error *error)
{
    if (!frontend || !application || !graph || !refs || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration import requires its isolated source manager and content graph");
    frontend_config_store *manager=frontend_config_store_create(frontend,error);
    if (!manager) return false;
    manager->restoring=true; manager->restore_refs=*refs;
    qa_source_save_io io={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && config_header(&io,&count);
    frontend_config_source **tail=&manager->sources;
    for (size_t i=0;ok && i<count;++i) {
        frontend_config_source *source=calloc(1,sizeof(*source));
        if (!source) { ok=fail(error,QA_ERROR_MEMORY,"Decoding real source configuration continuation"); break; }
        source->manager=manager; source->application=application; source->imported=true;
        source->published=source->configured=source->released=true;
        ok=config_row(source,&io,graph,keys,refs);
        for (frontend_config_source *previous=manager->sources;ok && previous;previous=previous->next)
            ok=strcmp(previous->saved_instance,source->saved_instance)!=0 &&
                (!source->keys || previous->keys!=source->keys);
        if (!ok) { source_destroy(source,NULL); break; }
        *tail=source; tail=&source->next;
    }
    qa_bytes clients={0};
    if (ok) ok=config_blob(&io,&clients) && clients.size &&
        frontend_remote_configs_restore(manager->clients,application,graph,keys,clients,error);
    qa_bytes neutral={0};
    if (ok) ok=config_blob(&io,&neutral) && neutral.size &&
        frontend_neutral_configs_restore(manager->neutral,graph,neutral,error);
    bool shared=false;
    if (ok) ok=qa_source_save_bool(&io,&shared) &&
        (shared || (!count && frontend_remote_configs_empty(manager->clients) && frontend_neutral_configs_empty(manager->neutral)));
    qa_bytes storage={0};
    if (ok && shared) {
        ok=config_blob(&io,&storage) && storage.size;
        if (ok) {
            manager->restored_storage.data=malloc(storage.size);
            ok=manager->restored_storage.data!=NULL;
            if (ok) { memcpy(manager->restored_storage.data,storage.data,storage.size);
                manager->restored_storage.size=storage.size; manager->storage_graph=graph; }
            else fail(error,QA_ERROR_MEMORY,"Retaining pure shared storage prefix before input-store binding");
        }
    }
    if (ok) ok=qa_source_save_bool(&io,&manager->image_fov_touched) &&
        qa_source_save_bool(&io,&manager->sticky_seed_pending) &&
        (!manager->image_fov_touched || shared) &&
        (!manager->sticky_seed_pending || (shared && !count && frontend_remote_configs_empty(manager->clients)));
    qa_bytes admin={0}; bool early=false;
    if (ok) ok=qa_source_save_bool(&io,&early);
    if (ok && early) {
        ok=frontend_save_text(&io,&manager->admin_source) && manager->admin_source && *manager->admin_source &&
            qa_source_save_bytes(&io,&manager->admin_identity,sizeof(manager->admin_identity)) &&
            config_blob(&io,&admin) && admin.size;
        frontend_config_source *source=manager->sources;
        while (ok && source && strcmp(source->saved_instance,manager->admin_source)) source=source->next;
        if (ok) ok=source && source->primary && qa_sha256_equal(&source->saved_identity,&manager->admin_identity);
    }
    bool logging=false; qa_bytes logfile={0};
    if (ok) ok=qa_source_save_bool(&io,&logging);
    if (ok && logging) ok=config_blob(&io,&logfile) && logfile.size &&
        frontend_qw_logfile_restore(graph,logfile,&manager->qw_logfile,error);
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok && early) ok=frontend_source_admin_restore(frontend,admin,&manager->admin,error);
    if (!ok) { frontend_config_store_destroy(manager,NULL); return false; }
    *out=manager; return true;
}
bool frontend_config_store_restore_into(frontend_config_store *manager,qa_application *application,
    qa_application_content_graph *graph,frontend_keys *keys,const frontend_keys_cvar_refs *refs,
    qa_bytes bytes,qa_error *error)
{
    if (!manager || manager->sources || !frontend_remote_configs_empty(manager->clients) || !frontend_neutral_configs_empty(manager->neutral) ||
        manager->variable_carries || manager->restoring || manager->running || manager->prepared || manager->shared ||
        manager->storage || manager->restored_storage.data || manager->storage_graph || manager->admin || manager->admin_source || manager->qw_logfile)
        return fail(error,QA_ERROR_ARGUMENT,"Pure import needs the constructor's empty stable configuration manager");
    frontend_config_store *decoded=NULL;
    if (!frontend_config_store_restore(manager->frontend,application,graph,keys,refs,bytes,&decoded,error)) return false;
    manager->sources=decoded->sources; manager->restoring=true; manager->restore_refs=*refs;
    manager->restored_storage=decoded->restored_storage; decoded->restored_storage=(qa_buffer){0};
    manager->storage_graph=decoded->storage_graph;
    manager->image_fov_touched=decoded->image_fov_touched;
    manager->sticky_seed_pending=decoded->sticky_seed_pending;
    manager->admin=decoded->admin; decoded->admin=NULL;
    manager->admin_source=decoded->admin_source; decoded->admin_source=NULL;
    manager->admin_identity=decoded->admin_identity;
    manager->qw_logfile=decoded->qw_logfile; decoded->qw_logfile=NULL;
    frontend_remote_configs_destroy(manager->clients,NULL);
    manager->clients=decoded->clients; decoded->clients=NULL;
    frontend_remote_configs_rebind(manager->clients,manager->frontend,manager);
    frontend_neutral_configs_destroy(manager->neutral,NULL);
    manager->neutral=decoded->neutral; decoded->neutral=NULL;
    frontend_neutral_configs_rebind(manager->neutral,manager->frontend,manager);
    for (frontend_config_source *source=manager->sources;source;source=source->next) source->manager=manager;
    decoded->sources=NULL; free(decoded); return true;
}
bool frontend_config_store_restore_registry_options(frontend_config_store *manager,const char *name,
    uint32_t logical,qa_cvar_options *options,frontend_client_registry_context *callback,
    qa_sha256_digest *identity,qa_error *error)
{
    if (!manager || !manager->restoring || manager->running || !name || !*name || !options || !callback || !identity)
        return fail(error,QA_ERROR_ARGUMENT,"Canonical registry prefix needs its decoded source configuration row");
    frontend_config_source *source=manager->sources;
    while (source && (!source->saved_instance || strcmp(source->saved_instance,name))) source=source->next;
    size_t index=source?seat_index(source,logical):0;
    config_seat *seat=source && index<source->seat_count?source->seats+index:NULL;
    if (!seat || !seat->cvars_transferred || !seat->registry_instance || seat->registry || seat->cvars)
        return fail(error,QA_ERROR_FORMAT,"Canonical registry prefix differs from its saved physical source seat");
    *options=registry_options(source,source->command.dialect);
    *callback=(frontend_client_registry_context){source,registry_context_retain,registry_context_release};
    *identity=source->saved_identity; return true;
}
typedef struct restored_source_scope {
    const qa_launch_instance *selected;
    const qa_command_context *command;
    const qa_cvars *cvars;
} restored_source_scope;
static bool restored_resolve(void *context,const char *name,qa_actor_owner *owner,qa_error *error)
{
    const restored_source_scope *scope=context;
    if (!name || strcmp(name,scope->selected->selection.instance) || !scope->command->owner || scope->command->owner>UINT32_MAX)
        return fail(error,QA_ERROR_FORMAT,"Restored key reference leaves its exact physical source instance");
    *owner=(qa_actor_owner)scope->command->owner; return true;
}
static bool restored_qualify(void *context,const qa_application_console_scope *registry,
    const qa_cvars *cvars,qa_error *error)
{
    const restored_source_scope *scope=context;
    return (registry && registry->provider==scope->command->owner &&
        registry->kind==QA_APPLICATION_CONSOLE_Q3_GAME && !registry->seat && cvars==scope->cvars) ||
        fail(error,QA_ERROR_FORMAT,"Restored key registry differs from its canonical decoded GAME owner");
}
static bool restore_source(void *context,qa_application *application,const qa_launch_snapshot *candidate,
    const qa_application_startup_source *authority,qa_error *error)
{
    const qa_launch_instance *selected=authority?authority->descriptor:NULL;
    qa_console *console=authority?authority->console:NULL; qa_cvars *cvars=authority?authority->cvars:NULL;
    const qa_command_context *command=authority?&authority->command:NULL;
    frontend_config_store *manager=context;
    if (authority && client_scope(authority->scope))
        return frontend_config_store_restore_client(manager,application,candidate,authority,error);
    if (!manager->restoring || !selected || !console || !cvars || !command ||
        !qa_application_command_context_active(application,command) || !command->owner ||
        qa_cvars_dialect(cvars)!=command->dialect || !game_scope(authority->scope) ||
        frontend_config_store_source(manager,console))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration import needs its exact decoded source constructor tuple");
    frontend_config_source *source=manager->sources;
    while (source && (!source->saved_instance || strcmp(source->saved_instance,selected->selection.instance))) source=source->next;
    if (!source || !source->imported || source->application!=application ||
        !qa_sha256_equal(&source->saved_identity,&selected->identity) || source->command.dialect!=command->dialect ||
        !qa_launch_snapshot_find(candidate,selected->selection.instance) || source->scope.kind!=authority->scope.kind ||
        source->scope.seat!=authority->scope.seat)
        return fail(error,QA_ERROR_FORMAT,"Decoded source configuration differs from its actual retained implementation");
    const qa_product *prepared=qa_catalog_product(frontend_config_files_catalog(source->files),
        frontend_config_files_product(source->files));
    const qa_product *actual=qa_catalog_product(qa_launch_instance_catalog(selected),selected->selection.product);
    const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    if (!prepared || !actual || strcmp(prepared->key,actual->key) ||
        source->primary!=(entities && !strcmp(entities->instance,selected->selection.instance)))
        return fail(error,QA_ERROR_FORMAT,"Decoded configuration product or primary role differs from its actual source");
    const qa_launch_choices *choices=qa_launch_snapshot_choices(candidate);
    size_t physical_seats=source->primary && !manager->frontend->options.dedicated?
        manager->frontend->options.seats:0;
    if (source->seat_count!=physical_seats || !choices || choices->seat_count<source->seat_count)
        return fail(error,QA_ERROR_FORMAT,"Decoded configuration lacks its actual authored seat roster");
    for (size_t i=0;i<source->seat_count;++i) {
        if (!choices || i>=choices->seat_count || source->seats[i].logical!=choices->seats[i].id)
            return fail(error,QA_ERROR_FORMAT,"Decoded input profile changed its physical-to-authored seat mapping");
        qa_console_dialect movement;
        if (!seat_movement(candidate,source->seats[i].logical,&movement,error) ||
            movement!=source->seats[i].movement_dialect)
            return fail(error,QA_ERROR_FORMAT,"Decoded input profile changed its selected seat movement source");
    }
    if (!qa_launch_instance_retain_metadata(selected,&source->metadata,error)) return false;
    source->console=console; source->cvars=cvars; source->command=*command;
    source->scope=authority->scope; source->declaration_owner=authority->declaration_owner;
    if (source->keys) {
        restored_source_scope key_scope={selected,command,cvars};
        frontend_keys_cvar_refs refs={.context=&key_scope,.resolve=restored_resolve,.qualify=restored_qualify};
        if (!frontend_key_profile_bind(source->keys,cvars,&refs,error)) return false;
    }
    if (!install_commands(source,error)) return false;
    source->imported=false; return true;
}
bool frontend_config_source_restore_seat_cvars(frontend_config_source *source,uint32_t logical,
    qa_cvars *cvars,const frontend_keys_cvar_refs *refs,qa_error *error)
{
    size_t index=source?seat_index(source,logical):0;
    config_seat *seat=source && index<source->seat_count?source->seats+index:NULL;
    (void)refs;
    if (!seat || !source->manager->restoring || !seat->cvars_transferred || !seat->registry_instance || !cvars)
        return fail(error,QA_ERROR_ARGUMENT,"Restored client registry needs its genuine decoded source reference");
    const qa_launch_instance *physical=NULL;
    uint32_t authored=0;
    const frontend_client_registry *owner=frontend_client_registry_lookup(source->manager->frontend,cvars);
    if (!owner || !frontend_client_registry_source(owner,&physical,&authored) ||
        authored!=logical || strcmp(physical->selection.instance,seat->registry_instance) ||
        !frontend_client_registry_matches(owner,instance(source),logical))
        return fail(error,QA_ERROR_FORMAT,"Restored client registry leaves its canonical physical source seat owner");
    if (seat->cvars && seat->cvars!=cvars)
        return fail(error,QA_ERROR_FORMAT,"Restored client alias replaced its canonical decoded registry");
    seat->cvars=cvars; seat->registry_bound=true; return true;
}
bool frontend_config_source_restore_seat_registry(frontend_config_source *source,uint32_t logical,
    frontend_client_registry *registry,const frontend_keys_cvar_refs *refs,qa_error *error)
{
    size_t index=source?seat_index(source,logical):0;
    config_seat *seat=source && index<source->seat_count?source->seats+index:NULL;
    if (!seat || !frontend_client_registry_matches(registry,instance(source),logical) ||
        (seat->registry && seat->registry!=registry))
        return fail(error,QA_ERROR_FORMAT,"Restored registry alias differs from its actual source seat owner");
    if (!frontend_config_source_restore_seat_cvars(source,logical,
        frontend_client_registry_cvars(registry),refs,error)) return false;
    return seat->registry || frontend_client_registry_retain(registry,&seat->registry,error);
}
bool frontend_config_store_finish_restore(frontend_config_store *manager,qa_error *error)
{
    if (!manager || !manager->restoring) return fail(error,QA_ERROR_ARGUMENT,"Configuration roster has no pending pure admission");
    qa_frontend *frontend=manager->frontend;
    if (!frontend_global_settings_storage_idle(frontend->global_settings_storage))
        return fail(error,QA_ERROR_FORMAT,"Restored configuration lacks its actual global directory owner");
    qa_settings_store global_user=frontend_global_settings_storage_user_store(frontend->global_settings_storage);
    qa_settings_store global_devices=frontend_global_settings_storage_device_store(frontend->global_settings_storage);
    for (frontend_config_source *source=manager->sources;source;source=source->next) {
        if (source->imported || !source->metadata || !source->console || !source->cvars)
            return fail(error,QA_ERROR_FORMAT,"Decoded configuration source lacks its physical registry binding");
        if (!frontend_config_files_global_current(source->files,global_user,global_devices))
            return fail(error,QA_ERROR_FORMAT,"Decoded source configuration differs from its retained global directories");
        for (size_t i=0;i<source->seat_count;++i) if (!source->seats[i].cvars ||
            (source->seats[i].cvars_transferred && !source->seats[i].registry_bound))
            return fail(error,QA_ERROR_FORMAT,"Decoded configuration client lacks its canonical registry binding");
    }
    if (manager->restored_storage.data && !manager->storage) {
        if (!frontend_shared_storage_restore(manager->storage_graph,
            (qa_bytes){manager->restored_storage.data,manager->restored_storage.size},global_user,global_devices,
            frontend_config_store_input_store(manager),!manager->frontend->options.dedicated,&manager->storage,error)) return false;
    }
    if (manager->storage && !shared_storage_current(manager))
        return fail(error,QA_ERROR_FORMAT,"Restored shared storage differs from its actual source and sticky input authorities");
    qa_settings_store sticky={0};
    if (manager->sticky_seed_pending && (!frontend_shared_storage_input(manager->storage,&sticky) || !sticky.vfs))
        return fail(error,QA_ERROR_FORMAT,"Saved sticky preference seed lacks its genuine admitted product store");
    if (!frontend_remote_configs_finish_restore(manager->clients,error)) return false;
    if (!frontend_neutral_configs_finish_restore(manager->neutral,error)) return false;
    if (manager->qw_logfile && !frontend_qw_logfile_finish_restore(manager->qw_logfile,error)) return false;
    if (manager->admin) {
        frontend_config_source *source=manager->sources;
        while (source && (!source->metadata || strcmp(instance(source)->selection.instance,manager->admin_source))) source=source->next;
        if (!source || !source->primary || !qa_sha256_equal(&instance(source)->identity,&manager->admin_identity) ||
            !frontend_source_admin_finish_restore(manager->admin,source->application,source->console,source->cvars,
                &source->command,global_user,error)) return false;
        free(manager->admin_source); manager->admin_source=NULL;
    }
    qa_buffer_free(&manager->restored_storage); manager->storage_graph=NULL;
    manager->storage_seeded=manager->storage!=NULL;
    manager->restoring=false; return true;
}
bool frontend_config_store_restore_client(frontend_config_store *manager,qa_application *application,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,qa_error *error)
{
    if (!manager || !manager->restoring || !source || !client_scope(source->scope))
        return fail(error,QA_ERROR_ARGUMENT,"CLIENT factory bind requires its decoded configuration roster");
    qa_application_startup_source actual;
    if (!qa_application_q3_client_configuration_read(application,source->scope.provider,source->scope.seat,&actual,error) ||
        actual.console!=source->console || actual.cvars!=source->cvars || !same_scope(actual.scope,source->scope) ||
        !source->descriptor || actual.descriptor->storage!=source->descriptor->storage)
        return fail(error,QA_ERROR_FORMAT,"CLIENT factory bind differs from its actual retained console slot");
    return frontend_remote_config_bind_restored(manager->clients,application,candidate,source,error);
}
