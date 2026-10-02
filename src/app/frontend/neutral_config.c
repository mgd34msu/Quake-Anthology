#include "neutral_config.h"
#include "internal.h"
#include "authored_bindings.h"
#include "global_settings_storage.h"
#include "input_profile.h"
#include "config_store.h"
#include "shared_settings.h"
#include "shared_publication.h"
#include "network_config.h"
#include "seat_save.h"
#include "shared_register.h"
#include "legacy_render_policy.h"
#include "save_private.h"
#include "qa/catalog_save.h"
#include "qa/console_cvar_observer.h"
#include "qa/console_cvars_prepare.h"
#include "qa/cvars_save.h"
#include "qa/source_frame_time.h"
#include "qa/application_native_q3_cvars.h"
#include "qa/q3_product_policy.h"
#include "qa/input_release.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_neutral_config {
    frontend_neutral_config *next;
    frontend_neutral_configs *owner;
    qa_launch_instance_lease *metadata;
    qa_application_client_source source;
    qa_command_context command;
    frontend_config_files *files;
    frontend_startup_config *phase;
    qa_application_client_preparation *preparation;
    frontend_authored_bindings *authored;
    qa_input_console *bindings;
    qa_input_seat *input;
    qa_input_seat *retirement_input;
    qa_input_release *retirement_release;
    qa_cvars *client, *mouse, *movement;
    qa_cvar_archive client_archive, mouse_archive, movement_archive, shared_archive;
    qa_seat_settings settings;
    char *saved_instance;
    qa_sha256_digest saved_identity;
    uint32_t physical_seat, saved_seat;
    uint64_t namespace_revision;
    qa_movement_kind kind;
    qa_console_dialect dialect;
    bool issued, attached, found, ready, published, running, imported, retiring, movement_selected;
    bool write_registered,dump_registered;
    bool configuration_done,variables_seeded,archive_seeded,release_before,startup_owned;
    bool retirement_started,recipient_returned;
    qa_error failure;
};
struct frontend_neutral_configs {
    qa_frontend *frontend;
    frontend_config_store *manager;
    frontend_neutral_config *rows;
    bool restoring;
};
static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e,code,0,"%s",message); return false; }
static const char *dialect_name(qa_console_dialect dialect)
{
    static const char *const names[]={"q1-netquake","q1-quakeworld","q2-classic","q2-rerelease","q3"};
    return dialect<=QA_CONSOLE_Q3?names[dialect]:NULL;
}
static const qa_launch_instance *descriptor(const frontend_neutral_config *row)
{ return row?qa_launch_instance_lease_view(row->metadata):NULL; }
static bool physical_read(const frontend_neutral_config *row,qa_application_client_source *out,bool connection)
{
    const qa_launch_instance *held=descriptor(row);
    return row && row->attached && held &&
        (connection?qa_application_client_read(row->owner->frontend->application,row->source.context.receiver,
            row->source.context.seat,out,NULL):
            qa_application_client_physical_read(row->owner->frontend->application,row->source.context.receiver,
                row->source.context.seat,out,NULL)) && out->descriptor &&
        out->context.lifetime==row->source.context.lifetime && out->context.session==row->source.context.session &&
        out->descriptor->selection.clock.kind==held->selection.clock.kind &&
        !strcmp(out->descriptor->selection.instance,held->selection.instance) &&
        !strcmp(out->descriptor->selection.implementation,held->selection.implementation) &&
        out->context.console==row->source.context.console && out->context.cvars==row->client &&
        out->context.physical_seat==row->physical_seat;
}
static bool physical(const frontend_neutral_config *row,qa_application_client_source *out)
{ return physical_read(row,out,true); }
static bool same_context(const qa_command_context *a,const qa_command_context *b)
{
    return a && b && a->owner==b->owner && a->session==b->session && a->seat==b->seat &&
        a->client==b->client && a->origin==b->origin && a->dialect==b->dialect &&
        a->registry==b->registry && a->generation==b->generation &&
        a->direct==b->direct && a->console_text==b->console_text && qa_actor_id_equal(a->actor,b->actor);
}
static bool active(const frontend_neutral_config *row,const qa_command_context *command)
{
    qa_application_client_source actual;
    return row && row->attached && !row->retiring && !row->imported &&
        physical_read(row,&actual,false) && same_context(command,&actual.context.command) &&
        qa_application_command_context_active(row->owner->frontend->application,command);
}
static void print(void *context,const qa_command_context *command,const char *text)
{
    frontend_neutral_config *row=context;
    if (row && command) frontend_console_print(row->owner->frontend,command,text);
}
static void binding_print(void *context,const char *text)
{ frontend_neutral_config *row=context; print(row,&row->command,text); }
static qa_cvars *route(void *context,const qa_command_context *command,const char *name)
{
    frontend_neutral_config *row=context;
    if (!active(row,command) || !name) return NULL;
    if (frontend_legacy_source_owns(row->client,name)) return row->client;
    qa_cvars *engine=qa_application_cvars(row->owner->frontend->application);
    frontend_shared_settings *shared=row->preparation?frontend_config_store_shared(row->owner->manager,
        row->owner->frontend->application,NULL):NULL;
    frontend_shared_values *values=frontend_shared_settings_values(shared);
    if (frontend_shared_values_prepared(values)) {
        qa_cvars_edit *ticket=NULL; qa_cvars *actual=NULL;
        if (!frontend_shared_values_client_access(values,row->preparation,command,&actual,&ticket,NULL)) return NULL;
        if (qa_cvars_edit_find(ticket,name)) return actual;
    } else if (engine && qa_cvars_find(engine,name)) return engine;
    if (qa_cvars_find(row->mouse,name)) return row->mouse;
    if (qa_cvars_find(row->movement,name)) return row->movement;
    return row->client;
}
static qa_cvars *visible(void *context,const qa_command_context *command,size_t ordinal)
{
    frontend_neutral_config *row=context;
    if (!active(row,command)) return NULL;
    qa_cvars *engine=qa_application_cvars(row->owner->frontend->application);
    if (engine) { if (!ordinal) return engine; --ordinal; }
    if (!ordinal) return row->mouse;
    --ordinal;
    if (row->movement!=row->client) { if (!ordinal) return row->movement; --ordinal; }
    return ordinal?NULL:row->client;
}
static bool edit(void *context,const qa_command_context *command,qa_cvars *registry,
    qa_cvars_edit **out,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (!out || !active(row,command) || (registry!=row->client && registry!=row->mouse &&
        registry!=row->movement && registry!=qa_application_cvars(row->owner->frontend->application)))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral cvar edit leaves its actual physical CLIENT");
    *out=NULL;
    frontend_shared_settings *shared=row->preparation?frontend_config_store_shared(row->owner->manager,
        row->owner->frontend->application,NULL):NULL;
    frontend_shared_values *values=frontend_shared_settings_values(shared);
    if (registry==qa_application_cvars(row->owner->frontend->application) && frontend_shared_values_prepared(values)) {
        qa_cvars *actual=NULL;
        return frontend_shared_values_client_access(values,row->preparation,command,&actual,out,e) && actual==registry;
    }
    return qa_cvars_observer_idle(registry) || fail(e,QA_ERROR_ARGUMENT,"Neutral CLIENT cannot borrow an outstanding registry edit");
}
static bool input_context(void *context,uint32_t physical,const qa_command_context *command,qa_error *e)
{
    frontend_neutral_config *row=context;
    return (row && physical==row->physical_seat && active(row,command)) ||
        fail(e,QA_ERROR_ARGUMENT,"Neutral input leaves its actual authored physical CLIENT");
}
static qa_input_seat *binding_seat(void *context,const qa_command_context *command)
{
    frontend_neutral_config *row=context;
    if (!active(row,command)) return NULL;
    qa_frontend *f=row->owner->frontend;
    return row->published?f->seats[row->physical_seat].input:row->input;
}
typedef struct neutral_filter {
    frontend_neutral_config *row;
    const qa_command_context *command;
} neutral_filter;
static bool archive_visible(void *context,const qa_cvars *registry,const qa_cvar_view *value)
{
    neutral_filter *filter=context;
    return route(filter->row,filter->command,value->name)==registry;
}
static bool config_command(void *context,const qa_command_invocation *invocation,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (!active(row,&invocation->context)) return fail(e,QA_ERROR_ARGUMENT,"Neutral config writer lost its physical CLIENT");
    if (!strcmp(invocation->argv[0],"condump")) {
        if (invocation->argc!=2) { print(row,&invocation->context,"condump <filename>\n"); return true; }
        qa_seat_console *console=row->owner->frontend->seats[row->physical_seat].console;
        return console && frontend_config_files_dump(row->files,invocation->argv[1],&invocation->context,
            qa_seat_console_buffer(console),e);
    }
    if (invocation->argc>2) { print(row,&invocation->context,"writeconfig [filename]\n"); return true; }
    const char *name=invocation->argc==2?invocation->argv[1]:"config.cfg";
    size_t length=strlen(name); bool suffix=length>=4 && !strcmp(name+length-4,".cfg");
    if (length>SIZE_MAX-5) return fail(e,QA_ERROR_MEMORY,"Neutral configuration filename exceeds storage");
    char *path=malloc(length+(suffix?1:5));
    if (!path) return fail(e,QA_ERROR_MEMORY,"Retaining neutral configuration filename");
    memcpy(path,name,length); memcpy(path+length,suffix?"":".cfg",suffix?1:5);
    qa_buffer text={0}; qa_input_seat *input=binding_seat(row,&invocation->context);
    bool ok=input && qa_input_bindings_config(input,false,&text,e);
    neutral_filter filter={row,&invocation->context};
    for (size_t i=0;ok;++i) {
        qa_cvars *registry=visible(row,&invocation->context,i);
        if (!registry) break;
        qa_cvars_edit *prepared=NULL; qa_buffer values={0};
        ok=edit(row,&invocation->context,registry,&prepared,e) && (prepared?
            qa_cvars_edit_config_filtered(prepared,archive_visible,&filter,&values,e):
            qa_cvars_config_filtered(registry,archive_visible,&filter,&values,e));
        if (ok && values.size>SIZE_MAX-text.size-1) ok=fail(e,QA_ERROR_MEMORY,"Neutral configuration text exceeds storage");
        uint8_t *next=ok?realloc(text.data,text.size+values.size+1):NULL;
        if (ok && !next) ok=fail(e,QA_ERROR_MEMORY,"Retaining neutral configuration text");
        if (ok) { text.data=next; if (values.size) memcpy(text.data+text.size,values.data,values.size);
            text.size+=values.size; text.data[text.size]=0; }
        qa_buffer_free(&values);
    }
    if (ok) ok=frontend_config_files_write_config_text(row->files,path,&invocation->context,(qa_bytes){text.data,text.size},e);
    qa_buffer_free(&text); free(path); return ok;
}
static bool script_read(void *context,frontend_script_scope scope,const char *name,
    const qa_command_context *command,qa_bytes *bytes,void **lease,qa_error *e)
{
    frontend_neutral_config *row=context;
    return active(row,command) && frontend_config_files_read(row->files,scope,name,command,bytes,lease,e);
}
static void script_release(void *context,void *lease)
{ frontend_neutral_config *row=context; frontend_config_files_release(row->files,lease); }
static bool read_script(void *context,const qa_command_context *command,const char *name,
    qa_bytes *bytes,void **lease,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (!active(row,command)) return fail(e,QA_ERROR_ARGUMENT,"Neutral script leaves its actual CLIENT");
    return row->phase?frontend_startup_config_read(row->phase,command,name,bytes,lease,e):
        frontend_config_files_console_read(row->files,name,command,bytes,lease,e);
}
static void script_complete(void *context,const qa_command_context *command,const char *name,bool success)
{
    frontend_neutral_config *row=context;
    if (row->phase) frontend_startup_config_script_complete(row->phase,command,name,success);
}
static bool defaults(void *context,qa_error *e)
{
    frontend_neutral_config *row=context;
    return frontend_authored_bindings_defaults(row->authored,row->input,(qa_console_dialect)row->kind,e);
}
static bool apply_archive_entries(frontend_neutral_config *row,const qa_cvar_archive *archive,qa_error *e)
{
    for (size_t i=0;i<archive->count;++i) {
        qa_cvars *registry=route(row,&row->command,archive->entries[i].name);
        qa_cvar_archive one={.entries=archive->entries+i,.count=1};
        frontend_shared_settings *shared=row->preparation?frontend_config_store_shared(row->owner->manager,
            row->owner->frontend->application,NULL):NULL;
        bool engine=registry==qa_application_cvars(row->owner->frontend->application);
        if (!registry || !(engine && shared?frontend_shared_values_archive(frontend_shared_settings_values(shared),&one,e):
            qa_cvar_archive_apply(registry,&one,e))) return false;
    }
    return true;
}
static bool archive(void *context,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (!apply_archive_entries(row,&row->client_archive,e) ||
        !apply_archive_entries(row,&row->mouse_archive,e) ||
        !apply_archive_entries(row,&row->movement_archive,e) ||
        !frontend_shared_values_archive(frontend_shared_settings_values(frontend_config_store_shared(row->owner->manager,
            row->owner->frontend->application,NULL)),&row->shared_archive,e)) return false;
    if (!row->found) return true;
    if (!qa_input_seat_replace_bindings(row->input,row->settings.bindings,row->settings.binding_count,e) ||
        !qa_input_mouse_settings_write(row->mouse,&row->settings.mouse,e)) return false;
    *qa_input_seat_gamepad_tuning(row->input)=row->settings.gamepad;
    if (row->settings.has_always_run && !qa_cvars_set_flags(row->mouse,"cl_run",
        row->settings.always_run?"1":"0",QA_CVAR_ARCHIVE,e)) return false;
    frontend_authored_bindings_profile(row->authored); return true;
}
static bool launch(void *context,qa_error *e)
{ (void)context; (void)e; return true; }
static bool replay(void *context,qa_error *e)
{
    frontend_neutral_config *row=context;
    return !row->startup_owned || qa_application_client_prepare_replay(row->preparation,e);
}
static bool startup_current(void *context,const qa_application_client_source *source)
{ return frontend_network_client_configuration_primary(context,source); }
static bool allow(void *context,const qa_command_invocation *invocation)
{
    frontend_neutral_config *row=context;
    return active(row,&invocation->context) && row->failure.code==QA_OK &&
        frontend_authored_bindings_observe(row->authored,invocation,&row->failure);
}
static bool initialize(void *context,const qa_launch_instance *selected,qa_cvars *client,
    const qa_command_context *command,qa_error *e)
{
    frontend_neutral_config *row=context; qa_frontend *f=row->owner->frontend;
    if (!selected || !client || !command || row->metadata || row->imported ||
        command->origin!=QA_COMMAND_SEAT || command->dialect>QA_CONSOLE_Q3 ||
        qa_cvars_dialect(client)!=command->dialect ||
        !qa_application_command_context_active(f->application,command) ||
        row->physical_seat>=f->options.seats || !f->seats)
        return fail(e,QA_ERROR_ARGUMENT,"Neutral configuration requires its real new CLIENT declarations");
    row->command=*command; row->client=client; row->dialect=command->dialect;
    if (!row->movement_selected) row->kind=(qa_movement_kind)row->dialect;
    qa_cvars *engine=qa_application_cvars(f->application);
    if (!engine || !qa_cvars_observer_idle(engine))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral configuration needs its returned canonical ENGINE");
    size_t count=qa_cvars_count(engine);
    if (count>SIZE_MAX/sizeof(qa_cvar_archive_entry)) return fail(e,QA_ERROR_MEMORY,"Canonical archive exceeds storage");
    row->shared_archive.entries=calloc(count?count:1,sizeof(qa_cvar_archive_entry));
    if (!row->shared_archive.entries) return fail(e,QA_ERROR_MEMORY,"Retaining the actual canonical archive");
    for (size_t i=0;i<count;++i) {
        const qa_cvar_view *value=qa_cvars_at(engine,i);
        const char *archived=qa_cvars_archive_value(engine,value);
        if (!archived) continue;
        qa_cvar_archive_entry *entry=row->shared_archive.entries+row->shared_archive.count++;
        entry->name=malloc(strlen(value->name)+1); entry->value=malloc(strlen(archived)+1);
        if (!entry->name || !entry->value) return fail(e,QA_ERROR_MEMORY,"Retaining canonical archive values");
        strcpy(entry->name,value->name); strcpy(entry->value,archived);
    }
    if (!qa_launch_instance_retain_metadata(selected,&row->metadata,e)) return false;
    qa_catalog *catalog=qa_launch_instance_catalog(selected);
    const qa_product *product=qa_catalog_product(catalog,selected->selection.product);
    if (!product) return fail(e,QA_ERROR_ARGUMENT,"Neutral CLIENT lost its actual catalog profile");
    if (product->family==QA_GAME_Q2 && !frontend_source_q2_settings_register(selected,client,command,e)) return false;
    if (row->dialect==QA_CONSOLE_Q3) {
        qa_q3_product_policy policy;
        if (product->family!=QA_GAME_Q3 || !product->builtin || product->program_kind!=QA_PROGRAM_BUILTIN ||
            !qa_application_q3_product_policy_read(f->application,&policy) || !policy.restriction_resolved ||
            policy.filesystem_restricted!=qa_catalog_q3_restricted(catalog))
            return fail(e,QA_ERROR_ARGUMENT,"Neutral Q3 CLIENT needs its retained compiled profile and resolved product policy");
        if (!qa_q3_product_policy_register_source(&policy,client,command->owner,e)) return false;
        qa_q3_product q3=product->campaign && !strcmp(product->campaign,"missionpack")?QA_Q3_TEAM_ARENA:QA_Q3_ARENA;
        for (size_t i=0;i<qa_native_q3_cvar_definition_count(q3);++i) {
            qa_native_q3_cvar_definition value;
            if (!qa_native_q3_cvar_definition_at(q3,i,&value))
                return fail(e,QA_ERROR_FORMAT,"Native Q3 CLIENT definition inventory changed during declaration");
            if (!qa_cvars_register(client,value.name,value.reset,value.flags,command->owner,NULL,e)) return false;
        }
    }
    row->files=frontend_config_files_create(catalog,product->id,
        frontend_global_settings_storage_user_store(f->global_settings_storage),
        frontend_global_settings_storage_device_store(f->global_settings_storage),e);
    if (!row->files) return false;
    qa_cvar_options options={.dialect=row->dialect};
    row->mouse=qa_cvars_create(&options,e);
    if ((qa_console_dialect)row->kind==row->dialect) row->movement=client;
    else { options.dialect=(qa_console_dialect)row->kind; row->movement=qa_cvars_create(&options,e); }
    return row->mouse && row->movement &&
        qa_input_mouse_settings_register(row->mouse,row->kind,e) &&
        qa_input_movement_settings_register(row->movement,row->kind,e) &&
        qa_source_frame_time_register(client,command->owner,e);
}
static bool install(void *context,const qa_application_client_source *source,bool restoring,qa_error *e)
{
    frontend_neutral_config *row=context; qa_frontend *f=row->owner->frontend;
    if (!source || !source->descriptor || !source->context.console || !source->context.cvars ||
        source->context.physical_seat!=row->physical_seat ||
        !qa_application_client_current(f->application,source))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral install requires its actual physical CLIENT tuple");
    if (restoring) {
        if (!row->imported || !row->saved_instance ||
            strcmp(row->saved_instance,source->descriptor->selection.instance) ||
            !qa_sha256_equal(&row->saved_identity,&source->descriptor->identity) ||
            source->context.command.dialect!=row->dialect || source->context.seat!=row->saved_seat ||
            !qa_launch_instance_retain_metadata(source->descriptor,&row->metadata,e))
            return fail(e,QA_ERROR_FORMAT,"Neutral import differs from its saved physical CLIENT");
        qa_catalog *catalog=qa_launch_instance_catalog(source->descriptor);
        const qa_product *actual=qa_catalog_product(catalog,source->descriptor->selection.product);
        const qa_product *saved=qa_catalog_product(frontend_config_files_catalog(row->files),
            frontend_config_files_product(row->files));
        if (!actual || !saved || strcmp(actual->key,saved->key) ||
            !frontend_config_files_global_current(row->files,
                frontend_global_settings_storage_user_store(f->global_settings_storage),
                frontend_global_settings_storage_device_store(f->global_settings_storage)))
            return fail(e,QA_ERROR_FORMAT,"Neutral files differ from their real CLIENT product and global stores");
        row->client=source->context.cvars; row->command=source->context.command;
        if ((qa_console_dialect)row->kind==row->dialect) row->movement=row->client;
        row->imported=false;
    }
    const qa_launch_instance *held=descriptor(row);
    if (!held || held->storage!=source->descriptor->storage || row->client!=source->context.cvars ||
        !same_context(&row->command,&source->context.command))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral install changed its actual registry constructor");
    row->source=*source; row->attached=true;
    if (!restoring) {
        qa_input_seat *live=f->seats[row->physical_seat].input;
        if (!live) return fail(e,QA_ERROR_ARGUMENT,"Neutral CLIENT lacks its actual physical input");
        qa_input_seat_options input={.seat=row->physical_seat,.context=row->command,
            .console=source->context.console,.cvars=row->mouse,.gamepad=*qa_input_seat_gamepad_tuning(live),
            .context_ready=input_context,.context_user=row};
        row->input=qa_input_seat_create(&input,e);
        row->authored=frontend_authored_bindings_create(e);
        if (!row->input || !row->authored) return false;
        if (!frontend_config_store_neutral_adopt_store(row->owner->manager,held,row->files,e)) return false;
        qa_settings_store store=frontend_config_files_store(row->files,false);
        qa_settings_store input_store=frontend_config_store_input_store(row->owner->manager);
        if (!input_store.vfs || !input_store.mount)
            return fail(e,QA_ERROR_ARGUMENT,"Neutral CLIENT lacks its actual sticky input ConfigStore");
        char logical[16],path[64]; snprintf(logical,sizeof(logical),"%" PRIu32,row->command.seat);
        snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)row->command.seat+1);
        const qa_product *product=qa_catalog_product(qa_launch_instance_catalog(held),held->selection.product);
        const char *client_owner[]={"client",product->key,held->selection.implementation,logical};
        const char *mouse_owner[]={"input",dialect_name(row->dialect),logical};
        const char *movement_owner[]={"movement",dialect_name((qa_console_dialect)row->kind)};
        if (!qa_settings_load_cvars(store,client_owner,4,row->dialect,&row->client_archive,e) ||
            !qa_settings_load_cvars(input_store,mouse_owner,3,row->dialect,&row->mouse_archive,e) ||
            !qa_settings_load_cvars(input_store,movement_owner,2,(qa_console_dialect)row->kind,&row->movement_archive,e) ||
            !qa_settings_load_seat(input_store,path,&row->settings,&row->found,e)) return false;
    }
    row->bindings=qa_input_console_create(&(qa_input_console_options){.console=source->context.console,
        .owner=source->context.receiver,.user=row,.seat=binding_seat,.print=binding_print},e);
    if (!row->bindings) return false;
    row->write_registered=qa_console_register_owned(source->context.console,"writeconfig","Save the actual CLIENT configuration",
        source->context.receiver,source->context.receiver,true,config_command,row,e);
    if (!row->write_registered) return false;
    row->dump_registered=qa_console_register_owned(source->context.console,"condump","Save the actual seat console log",
        source->context.receiver,source->context.receiver,true,config_command,row,e);
    if (!row->dump_registered || restoring) return row->dump_registered;
    row->startup_owned=frontend_network_client_configuration_primary(f,source);
    return qa_application_client_prepare_begin(f->application,source,&row->preparation,e) &&
        (!row->startup_owned || qa_application_client_prepare_startup_claim(row->preparation,f,startup_current,e)) &&
        frontend_config_store_client_settings_begin(row->owner->manager,row->preparation,e);
}
static bool advance_preparation(void *context,qa_application_client_preparation *preparation,bool *complete,qa_error *e)
{
    frontend_neutral_config *row=context; qa_frontend *f=row->owner->frontend;
    *complete=false;
    if (qa_application_client_prepare_entered(preparation,QA_CLIENT_PREPARE_RELEASE)) {
        bool done=false;
        if (!frontend_config_store_client_settings_advance(row->owner->manager,preparation,row->release_before,&done,e)) return false;
        if (done && !row->release_before) { row->release_before=true; return true; }
        *complete=done; return true;
    }
    if (qa_application_client_prepare_entered(preparation,QA_CLIENT_PREPARE_RESOURCES)) {
        *complete=frontend_config_store_client_settings_prepare(row->owner->manager,preparation,e) &&
            frontend_seat_client_recipient_ready(f,row->physical_seat,&row->source,e);
        return *complete;
    }
    if (!qa_application_client_prepare_entered(preparation,QA_CLIENT_PREPARE_CONFIGURATION))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral programme is outside its actual CLIENT configuration phase");
    if (!row->archive_seeded) {
        if (!frontend_config_store_client_settings_seed(row->owner->manager,preparation,&row->shared_archive,e)) return false;
        row->archive_seeded=true;
    }
    if (!row->variables_seeded) {
        if (row->startup_owned && !qa_application_client_prepare_initial(preparation,e)) return false;
        row->variables_seeded=true;
    }
    if (!row->phase) {
        const qa_launch_instance *held=descriptor(row);
        bool safe=false;
        if (row->startup_owned && !qa_application_client_prepare_safe_mode(preparation,&safe,e)) return false;
        frontend_startup_config_options options={.command=row->command,.safe_mode=safe,
            .has_mod=qa_catalog_configuration_base(qa_launch_instance_catalog(held),held->selection.product)!=held->selection.product,
            .context=row,.read=script_read,.release=script_release,
            .apply_defaults=defaults,.apply_archive=archive,.apply_launch=launch,.replay_startup_variables=replay};
        row->phase=frontend_startup_config_create(&options,e);
        if (!row->phase) return false;
    }
    bool done=false; row->running=true;
    bool ok=frontend_startup_config_advance(row->phase,row->source.context.console,&done,e);
    row->running=false;
    if (row->failure.code!=QA_OK) { if (e) *e=row->failure; return false; }
    if (!ok || !done) return ok;
    frontend_authored_bindings_finish(row->authored);
    qa_input_seat *live=f->seats[row->physical_seat].input;
    int32_t controller=f->input?qa_input_platform_controller(f->input,row->physical_seat):-1;
    if ((controller>=0 && !qa_input_seat_remap_controller(row->input,controller,e)) ||
        !qa_input_seat_configuration_ready(live,row->input,e) ||
        !frontend_startup_config_destroy(row->phase,e)) return false;
    row->phase=NULL;
    row->configuration_done=true; *complete=true; return true;
}
static bool publication_ready(void *context,const qa_application_client_preparation *preparation)
{
    const frontend_neutral_config *row=context; const qa_frontend *f=row->owner->frontend;
    return row->preparation==preparation && row->configuration_done && !row->running && !row->phase && row->input &&
        qa_input_seat_configuration_owned_is(f->seats[row->physical_seat].input,row->input) &&
        frontend_seat_client_recipient_ready_is(f,row->physical_seat,&row->source) &&
        (!row->startup_owned || qa_application_client_prepare_startup_ready(preparation)) &&
        frontend_config_store_client_settings_ready_is(row->owner->manager,preparation);
}
static void publication_consume(void *context,qa_application_client_preparation *preparation)
{
    frontend_neutral_config *row=context; qa_frontend *f=row->owner->frontend;
    frontend_config_store_client_settings_consume(row->owner->manager,preparation);
    qa_input_seat_configuration_publish(f->seats[row->physical_seat].input,row->input);
    frontend_seat_client_recipient_publish(f,row->physical_seat,&row->source);
    if (row->startup_owned) qa_application_client_prepare_startup_publish(preparation);
    row->published=true;
}
static bool cleanup_preparation(void *context,qa_application_client_preparation *preparation,bool *complete,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (!row->published) {
        return frontend_config_store_client_settings_abort(row->owner->manager,preparation,complete,e);
    }
    return frontend_config_store_client_settings_finish(row->owner->manager,preparation,complete,e);
}
static bool cancel_preparation(void *context,qa_application_client_preparation *preparation,bool *complete,qa_error *e)
{
    frontend_neutral_config *row=context;
    return frontend_config_store_client_settings_cancel(row->owner->manager,preparation,complete,e);
}
static bool configuration_advance(void *context,const qa_application_client_source *source,
    qa_application_client_preparation *preparation,bool *complete,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (complete) *complete=false;
    if (!row || !source || !complete || row->preparation!=preparation || row->running || row->retiring ||
        source->context.console!=row->source.context.console || source->context.cvars!=row->client ||
        !qa_application_client_current(row->owner->frontend->application,source) ||
        !qa_application_client_prepare_entered(preparation,QA_CLIENT_PREPARE_CONFIGURATION))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral configuration step requires its exact entered CLIENT programme");
    return advance_preparation(row,preparation,complete,e);
}
static bool configure(void *context,const qa_application_client_source *source,bool *complete,qa_error *e)
{
    frontend_neutral_config *row=context; qa_frontend *f=row->owner->frontend;
    if (!source || !source->descriptor || !complete || row->running || !row->attached || row->retiring || row->imported ||
        source->context.console!=row->source.context.console || source->context.cvars!=row->client ||
        source->descriptor->storage!=descriptor(row)->storage || !qa_application_client_current(f->application,source))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral configuration lost its actual returned CLIENT");
    *complete=row->ready;
    if (row->ready) return true;
    if (!row->preparation) return fail(e,QA_ERROR_ARGUMENT,"Neutral programme has no retained CLIENT preparation");
    if (!qa_application_client_prepare_phase_is(row->preparation,QA_CLIENT_PREPARE_CLEANUP)) {
        if (qa_application_client_prepare_phase_is(row->preparation,QA_CLIENT_PREPARE_RESOURCES)) {
            bool done=false;
            bool ok=qa_application_client_prepare_advance(row->preparation,advance_preparation,row,&done,e);
            if (!ok || !done) return ok;
            if (!qa_application_client_prepare_consume(row->preparation,publication_ready,publication_consume,row,e)) return false;
        } else {
            bool done=false;
            return qa_application_client_prepare_advance(row->preparation,advance_preparation,row,&done,e);
        }
    }
    bool finished=false;
    bool ok=qa_application_client_prepare_finish(&row->preparation,cleanup_preparation,row,&finished,e);
    if (!finished) return ok;
    qa_input_seat_destroy(row->input); row->input=NULL;
    qa_cvar_archive_free(&row->client_archive); qa_cvar_archive_free(&row->mouse_archive);
    qa_cvar_archive_free(&row->movement_archive); qa_seat_settings_free(&row->settings);
    qa_cvar_archive_free(&row->shared_archive);
    row->ready=true; *complete=true; return ok;
}
static bool retire(void *context,const qa_application_client_source *source,qa_error *e)
{
    frontend_neutral_config *row=context;
    if (row->running || (row->attached && (!source || source->context.console!=row->source.context.console ||
        source->context.cvars!=row->client)) || (row->files && !frontend_config_files_idle(row->files)))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral retirement still retains its actual configuration programme");
    if (row->preparation) {
        if (qa_application_client_prepare_phase_is(row->preparation,QA_CLIENT_PREPARE_RELEASE)) {
            bool complete=false;
            if (!qa_application_client_prepare_cancel_advance(row->preparation,cancel_preparation,row,&complete,e)) {
                if (!qa_application_client_prepare_abort(row->preparation,e)) return false;
            } else if (!complete) return false;
        }
        if (!qa_application_client_prepare_phase_is(row->preparation,QA_CLIENT_PREPARE_CLEANUP) &&
            !qa_application_client_prepare_abort(row->preparation,e)) return false;
        bool complete=false;
        if (!qa_application_client_prepare_finish(&row->preparation,cleanup_preparation,row,&complete,e) || !complete) return false;
    }
    if (row->published && !row->recipient_returned) {
        qa_frontend *f=row->owner->frontend;
        qa_application_client_source actual;
        if (!f->seats || row->physical_seat>=f->options.seats || !physical_read(row,&actual,false))
            return fail(e,QA_ERROR_ARGUMENT,"CLIENT retirement lost its retained physical input namespace");
        qa_input_seat *input=f->seats[row->physical_seat].input;
        qa_console *console=NULL; qa_cvars *registry=NULL; qa_command_context command;
        if (!input || !qa_input_seat_recipient_read(input,&console,&registry,&command) ||
            console!=actual.context.console || registry!=actual.context.cvars || !active(row,&command) ||
            (row->retirement_input && row->retirement_input!=input))
            return fail(e,QA_ERROR_ARGUMENT,"CLIENT retirement cannot clear another physical recipient");
        qa_input_release_scope all={.all=true,.controller=-1};
        if (!row->retirement_release) {
            if (!qa_input_release_prepare(input,&all,(double)f->wall_time_ns/1000000.0,&row->retirement_release,e)) return false;
            row->retirement_input=input; row->retirement_started=true;
        }
        qa_input_release_outcome outcome=QA_INPUT_RELEASE_UNENTERED;
        if (!qa_input_release_advance(row->retirement_release,&outcome,e)) return false;
        if (outcome==QA_INPUT_RELEASE_WAITING) return false;
        if (outcome!=QA_INPUT_RELEASE_COMPLETED || !qa_input_release_ready(row->retirement_release,input,&all,e) ||
            !frontend_seat_engine_recipient_ready(f,row->physical_seat,&command,e)) return false;
        qa_input_release_publish(row->retirement_release); row->retirement_release=NULL; row->retirement_input=NULL;
        frontend_seat_engine_recipient_publish(f,row->physical_seat,&command); row->recipient_returned=true;
    }
    if (row->bindings && !qa_console_idle(row->source.context.console))
        return fail(e,QA_ERROR_ARGUMENT,"Neutral input handlers still have an entered physical console");
    qa_input_console_destroy(row->bindings);
    row->bindings=NULL;
    if (row->write_registered) qa_console_unregister(row->source.context.console,"writeconfig",row->command.owner);
    if (row->dump_registered) qa_console_unregister(row->source.context.console,"condump",row->command.owner);
    row->write_registered=row->dump_registered=false;
    if (!frontend_startup_config_destroy(row->phase,e)) return false;
    row->phase=NULL; row->retiring=true; return true;
}
static bool dispose(frontend_neutral_config *row,qa_error *e)
{
    if (row->running || row->preparation || row->retirement_release || row->bindings || row->phase ||
        (row->files && !frontend_config_files_destroy(row->files,e))) return false;
    row->files=NULL;
    qa_input_seat_destroy(row->input);
    if (row->movement!=row->client) qa_cvars_destroy(row->movement);
    qa_cvars_destroy(row->mouse);
    frontend_authored_bindings_destroy(row->authored);
    qa_cvar_archive_free(&row->client_archive); qa_cvar_archive_free(&row->mouse_archive);
    qa_cvar_archive_free(&row->movement_archive); qa_seat_settings_free(&row->settings);
    qa_cvar_archive_free(&row->shared_archive);
    qa_launch_instance_lease_release(row->metadata); free(row->saved_instance); free(row); return true;
}
static void released(void *context)
{
    frontend_neutral_config *row=context;
    frontend_neutral_config **at=&row->owner->rows;
    while (*at && *at!=row) at=&(*at)->next;
    if (*at!=row) return;
    frontend_neutral_config *next=row->next;
    row->attached=false;
    if (dispose(row,NULL)) *at=next;
}
frontend_neutral_configs *frontend_neutral_configs_create(qa_frontend *f,frontend_config_store *manager,qa_error *e)
{
    if (!f || !manager) return NULL;
    frontend_neutral_configs *owner=calloc(1,sizeof(*owner));
    if (!owner) { fail(e,QA_ERROR_MEMORY,"Retaining neutral CLIENT configurations"); return NULL; }
    owner->frontend=f; owner->manager=manager; return owner;
}
bool frontend_neutral_configs_empty(const frontend_neutral_configs *owner)
{ return !owner || !owner->rows; }
bool frontend_neutral_configs_destroy(frontend_neutral_configs *owner,qa_error *e)
{
    if (!owner) return true;
    for (frontend_neutral_config *row=owner->rows;row;row=row->next)
        if (row->attached || row->running || row->bindings || row->phase)
            return fail(e,QA_ERROR_ARGUMENT,"Neutral configurations retain a physical CLIENT");
    while (owner->rows) {
        frontend_neutral_config *row=owner->rows,*next=row->next;
        if (!dispose(row,e)) return false;
        owner->rows=next;
    }
    free(owner); return true;
}
bool frontend_neutral_config_options(frontend_neutral_configs *owner,uint32_t physical,
    qa_movement_kind movement,frontend_client_source_options *out,qa_error *e)
{
    if (!owner || !out || physical>=owner->frontend->options.seats || movement>QA_MOVEMENT_Q3)
        return fail(e,QA_ERROR_ARGUMENT,"Neutral options require an actual physical seat and selected movement receipt");
    frontend_neutral_config *row=NULL;
    if (owner->restoring) {
        for (row=owner->rows;row;row=row->next)
            if (row->imported && !row->issued && row->physical_seat==physical && row->kind==movement) break;
        if (!row) return fail(e,QA_ERROR_FORMAT,"Neutral constructor has no matching saved configuration");
    } else {
        row=calloc(1,sizeof(*row));
        if (!row) return fail(e,QA_ERROR_MEMORY,"Retaining the neutral CLIENT programme owner");
        row->owner=owner; row->physical_seat=physical; row->kind=movement; row->namespace_revision=1;
        row->next=owner->rows; owner->rows=row;
    }
    row->issued=true;
    row->movement_selected=true;
    *out=(frontend_client_source_options){.physical_seat=physical,.context=row,
        .initialize=initialize,.configure=configure,.install=install,.print=print,
        .configuration_advance=configuration_advance,
        .allow_command=allow,.cvar_owner=route,.visible_cvars=visible,.cvar_edit=edit,
        .read_script=read_script,.release_script=script_release,.script_complete=script_complete,
        .retire=retire,.released=released};
    return true;
}
bool frontend_neutral_config_pending_options(frontend_neutral_configs *owner,uint32_t physical,
    frontend_client_source_options *out,qa_error *e)
{
    if (!owner) return fail(e,QA_ERROR_ARGUMENT,"Neutral template needs its actual configuration owner");
    if (owner->restoring) {
        frontend_neutral_config *saved=NULL;
        for (frontend_neutral_config *row=owner->rows;row;row=row->next) {
            if (!row->imported || row->issued || row->physical_seat!=physical) continue;
            if (saved) return fail(e,QA_ERROR_FORMAT,"Saved physical seat has ambiguous neutral configuration rows");
            saved=row;
        }
        if (!saved) return fail(e,QA_ERROR_FORMAT,"Saved physical seat has no actual neutral configuration row");
        bool selected=saved->movement_selected;
        bool ok=frontend_neutral_config_options(owner,physical,saved->kind,out,e);
        if (ok) saved->movement_selected=selected;
        return ok;
    }
    if (!frontend_neutral_config_options(owner,physical,QA_MOVEMENT_NETQUAKE,out,e)) return false;
    frontend_neutral_config *row=out->context;
    row->movement_selected=false;
    return true;
}
bool frontend_neutral_config_options_cancel(frontend_neutral_configs *owner,
    frontend_client_source_options *options,qa_error *e)
{
    if (!owner || !options || options->released!=released)
        return fail(e,QA_ERROR_ARGUMENT,"Template cancellation needs its actual neutral configuration origin");
    frontend_neutral_config **at=&owner->rows;
    while (*at && *at!=options->context) at=&(*at)->next;
    if (!*at) { *options=(frontend_client_source_options){0}; return true; }
    frontend_neutral_config *row=*at,*next=row->next;
    if (row->attached || row->imported)
        return fail(e,QA_ERROR_ARGUMENT,"Template cancellation still retains its physical CLIENT owner");
    if (!retire(row,NULL,e) || !dispose(row,e)) return false;
    *at=next; *options=(frontend_client_source_options){0}; return true;
}
bool frontend_neutral_config_movement_adopt(frontend_neutral_configs *owner,const qa_console *console,
    qa_movement_kind movement,qa_error *e)
{
    if (!owner || !console || movement>QA_MOVEMENT_Q3)
        return fail(e,QA_ERROR_ARGUMENT,"Movement adoption needs the actual received recipe kind");
    for (frontend_neutral_config *row=owner->rows;row;row=row->next) {
        if (!row->attached || row->source.context.console!=console) continue;
        qa_application_client_source actual;
        if (!row->ready || row->running || row->phase || row->retiring || row->imported || !physical(row,&actual))
            return fail(e,QA_ERROR_ARGUMENT,"Movement adoption requires its completed physical CLIENT programme");
        if (row->movement_selected && row->kind==movement) return true;
        if (row->namespace_revision==UINT64_MAX || !qa_console_idle(row->source.context.console) ||
            !qa_cvars_observer_idle(row->client) || !qa_cvars_observer_idle(row->mouse) ||
            !qa_cvars_observer_idle(row->movement))
            return fail(e,QA_ERROR_ARGUMENT,"Movement replacement retains an entered physical CLIENT namespace");
        qa_cvars *next=row->client;
        if ((qa_console_dialect)movement!=row->dialect) {
            next=qa_cvars_create(&(qa_cvar_options){.dialect=(qa_console_dialect)movement},e);
            if (!next) return false;
        }
        qa_cvar_archive archive={0};
        const char *path[]={"movement",dialect_name((qa_console_dialect)movement)};
        bool ok=qa_input_movement_settings_register(next,movement,e) &&
            qa_settings_load_cvars(frontend_config_store_input_store(owner->manager),path,2,
                (qa_console_dialect)movement,&archive,e) && qa_cvar_archive_apply(next,&archive,e);
        qa_cvar_archive_free(&archive);
        if (!ok) { if (next!=row->client) qa_cvars_destroy(next); return false; }
        if (row->movement!=row->client) qa_cvars_destroy(row->movement);
        row->movement=next; row->kind=movement; row->movement_selected=true; ++row->namespace_revision;
        return true;
    }
    return fail(e,QA_ERROR_NOT_FOUND,"No actual neutral CLIENT owns movement adoption");
}
bool frontend_neutral_config_read(const frontend_neutral_configs *owner,const qa_console *console,
    frontend_neutral_config_view *out,qa_error *e)
{
    if (!owner || !console || !out) return fail(e,QA_ERROR_ARGUMENT,"Neutral read needs its actual CLIENT console");
    for (const frontend_neutral_config *row=owner->rows;row;row=row->next) {
        if (!row->attached || row->source.context.console!=console) continue;
        if (row->retiring || row->retirement_started || row->imported || row->running || !row->ready || !row->published || row->phase || row->input ||
            !qa_cvars_observer_idle(row->client) || !qa_cvars_observer_idle(row->mouse) || !qa_cvars_observer_idle(row->movement))
            return fail(e,QA_ERROR_ARGUMENT,"Neutral CLIENT settings have not completed their actual programme");
        qa_application_client_source actual;
        if (!physical(row,&actual)) return fail(e,QA_ERROR_ARGUMENT,"Neutral settings lost their actual installed CLIENT tuple");
        *out=(frontend_neutral_config_view){.owner=row,.source=actual,.client=row->client,
            .mouse=row->mouse,.movement=row->movement,.kind=row->kind,.physical_seat=row->physical_seat,
            .namespace_revision=row->namespace_revision,.ready=row->movement_selected,.published=true};
        return true;
    }
    return fail(e,QA_ERROR_NOT_FOUND,"No neutral configuration owns this CLIENT console");
}
bool frontend_neutral_config_current(const frontend_neutral_config_view *view)
{
    frontend_neutral_config_view actual;
    return view && view->owner && frontend_neutral_config_read(view->owner->owner,view->source.context.console,&actual,NULL) &&
        actual.owner==view->owner && actual.client==view->client && actual.mouse==view->mouse &&
        actual.movement==view->movement && actual.kind==view->kind && actual.physical_seat==view->physical_seat &&
        actual.namespace_revision==view->namespace_revision &&
        actual.ready==view->ready && actual.published==view->published &&
        qa_application_client_current(view->owner->owner->frontend->application,&view->source);
}
bool frontend_neutral_config_startup_read(const frontend_neutral_configs *owner,
    qa_application_client_source *out,bool *found,qa_error *e)
{
    if (!owner || !out || !found) return fail(e,QA_ERROR_ARGUMENT,"Startup CLIENT read needs its real configuration owner");
    *found=false; *out=(qa_application_client_source){0};
    for (const frontend_neutral_config *row=owner->rows;row;row=row->next) {
        if (!row->startup_owned || !row->attached || row->retiring || row->imported) continue;
        if (!row->ready || !row->published || row->running || row->preparation) continue;
        frontend_neutral_config_view view;
        if (!frontend_neutral_config_read(owner,row->source.context.console,&view,e)) return false;
        if (!frontend_network_client_configuration_primary(owner->frontend,&view.source)) continue;
        if (*found) return fail(e,QA_ERROR_ARGUMENT,"Startup has multiple real primary standalone CLIENT programmes");
        *out=view.source; *found=true;
    }
    return true;
}
bool frontend_neutral_config_client_input(const frontend_neutral_configs *owner,
    const qa_application_client_preparation *preparation,uint32_t ordinal,qa_input_seat **out,qa_error *e)
{
    if (out) *out=NULL;
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    const qa_frontend *f=owner?owner->frontend:NULL;
    if (!out || !f || !source || !qa_application_client_prepare_associated(f->application,preparation) ||
        !f->seats || ordinal>=f->options.seats)
        return fail(e,QA_ERROR_ARGUMENT,"CLIENT input lost its genuine preparation and physical roster");
    const frontend_neutral_config *row=owner->rows;
    while (row && (!row->attached || row->source.context.lifetime!=source->context.lifetime)) row=row->next;
    const qa_launch_instance *held=descriptor(row);
    if (!row || row->retiring || row->imported || !held || held->storage!=source->descriptor->storage ||
        row->client!=source->context.cvars || row->source.context.console!=source->context.console)
        return fail(e,QA_ERROR_ARGUMENT,"CLIENT input has no owned neutral configuration dictionary");
    const frontend_seat *physical=f->seats+ordinal;
    if (physical->frontend!=f || physical->id!=ordinal || !physical->input || !physical->console ||
        qa_input_seat_ordinal(physical->input)!=ordinal)
        return fail(e,QA_ERROR_ARGUMENT,"CLIENT input lost its unchanged installed physical root");
    if (ordinal==row->physical_seat && row->input) {
        if (qa_input_seat_ordinal(row->input)!=ordinal)
            return fail(e,QA_ERROR_ARGUMENT,"CLIENT staging dictionary belongs to another physical seat");
        *out=row->input; return true;
    }
    if (ordinal==row->physical_seat && !row->published)
        return fail(e,QA_ERROR_ARGUMENT,"Changed CLIENT input has no genuine staging dictionary");
    qa_command_context command=qa_seat_console_context_read(physical->console);
    if (!frontend_seat_context_ready((void *)physical,ordinal,&command,e)) return false;
    *out=physical->input; return true;
}
bool frontend_neutral_config_client_controller(const frontend_neutral_configs *owner,
    const qa_application_client_preparation *preparation,uint32_t ordinal,qa_controller_selection *out,qa_error *e)
{
    qa_input_seat *dictionary=NULL;
    if (!out || !frontend_neutral_config_client_input(owner,preparation,ordinal,&dictionary,e)) return false;
    const qa_application_client_source *source=qa_application_client_prepare_source(preparation);
    const frontend_neutral_config *row=owner->rows;
    while (row && (!row->attached || row->source.context.lifetime!=source->context.lifetime)) row=row->next;
    if (ordinal==row->physical_seat && !row->published && row->found) {
        *out=row->settings.controller; return true;
    }
    return (owner->frontend->input && qa_input_platform_selection(owner->frontend->input,ordinal,out)) ||
        fail(e,QA_ERROR_ARGUMENT,"CLIENT controller selection has no actual retained platform receipt");
}
void frontend_neutral_configs_rebind(frontend_neutral_configs *owner,qa_frontend *f,frontend_config_store *manager)
{ if (owner && f && manager) { owner->frontend=f; owner->manager=manager; } }
bool frontend_neutral_configs_save(frontend_neutral_configs *owner,qa_error *e)
{
    if (!owner || owner->restoring) return fail(e,QA_ERROR_ARGUMENT,"Neutral save requires its published roster");
    for (frontend_neutral_config *row=owner->rows;row;row=row->next) {
        if (!row->published) continue;
        frontend_neutral_config_view view;
        if (!frontend_neutral_config_read(owner,row->source.context.console,&view,e)) return false;
        const qa_launch_instance *held=descriptor(row);
        const qa_product *product=qa_catalog_product(qa_launch_instance_catalog(held),held->selection.product);
        qa_settings_store store=frontend_config_files_store(row->files,false);
        qa_settings_store input=frontend_config_store_input_store(owner->manager);
        if (!product || !input.vfs || !input.mount) return fail(e,QA_ERROR_ARGUMENT,"Neutral archive lost its actual product and sticky stores");
        char logical[16],path[64]; snprintf(logical,sizeof(logical),"%" PRIu32,row->command.seat);
        snprintf(path,sizeof(path),"input/seat-%" PRIu64 ".json",(uint64_t)row->command.seat+1);
        const char *client_owner[]={"client",product->key,held->selection.implementation,logical};
        const char *mouse_owner[]={"input",dialect_name(row->dialect),logical};
        const char *movement_owner[]={"movement",dialect_name((qa_console_dialect)row->kind)};
        qa_input_seat *live=owner->frontend->seats[row->physical_seat].input;
        qa_console_history *history=qa_seat_console_history(owner->frontend->seats[row->physical_seat].console);
        size_t count=qa_input_seat_binding_count(live),history_count=qa_console_history_count(history);
        if (count>SIZE_MAX/sizeof(qa_input_binding) || history_count>SIZE_MAX/sizeof(char *))
            return fail(e,QA_ERROR_MEMORY,"Neutral input archive exceeds storage");
        qa_input_binding *bindings=calloc(count?count:1,sizeof(*bindings));
        char **lines=malloc(history_count?history_count*sizeof(*lines):1);
        if (!bindings || !lines) { free(bindings); free(lines); return fail(e,QA_ERROR_MEMORY,"Retaining neutral physical input archive"); }
        for (size_t i=0;i<count;++i) {
            bindings[i]=*qa_input_seat_binding_at(live,i);
            if (bindings[i].input.kind==QA_PHYSICAL_BUTTON || bindings[i].input.kind==QA_PHYSICAL_AXIS)
                bindings[i].input.device=0;
        }
        for (size_t i=0;i<history_count;++i) lines[i]=(char *)qa_console_history_at(history,i);
        qa_input_command_tuning tuning;
        bool ok=qa_input_settings_read_routed(row->mouse,row->movement,row->kind,&tuning,e);
        qa_seat_settings settings={.bindings=bindings,.binding_count=count,
            .history=lines,.history_count=history_count,.gamepad=*qa_input_seat_gamepad_tuning(live)};
        if (ok) { settings.mouse=tuning.mouse; settings.has_always_run=true; settings.always_run=tuning.view.always_run; }
        qa_input_platform *platform=owner->frontend->input;
        qa_haptic_player *haptics=platform?qa_input_platform_haptics(platform,row->physical_seat):NULL;
        if (haptics) { settings.rumble=haptics->enabled; settings.rumble_strength=haptics->strength; }
        if (platform && !qa_input_platform_selection(platform,row->physical_seat,&settings.controller))
            ok=fail(e,QA_ERROR_ARGUMENT,"Neutral archive lost its actual selected controller");
        ok=ok && qa_settings_save_cvars(store,client_owner,4,row->client,e) &&
            qa_settings_save_cvars(input,mouse_owner,3,row->mouse,e) &&
            qa_settings_save_cvars(input,movement_owner,2,row->movement,e) &&
            qa_settings_save_seat(input,path,&settings,e);
        free(lines); free(bindings); if (!ok) return false;
    }
    return true;
}
bool frontend_neutral_configs_visit(const frontend_neutral_configs *owner,
    const qa_application_content_visitor *visitor,qa_error *e)
{
    if (!owner || owner->restoring || !visitor || !visitor->catalog || !visitor->pool || !visitor->view)
        return fail(e,QA_ERROR_ARGUMENT,"Neutral inventory requires its returned actual owners");
    for (const frontend_neutral_config *row=owner->rows;row;row=row->next) {
        frontend_neutral_config_view view;
        if (!frontend_neutral_config_read(owner,row->source.context.console,&view,e)) return false;
        const qa_launch_instance *held=descriptor(row); qa_catalog *catalog=qa_launch_instance_catalog(held);
        if (!frontend_config_files_visit(row->files,visitor,e) ||
            !visitor->pool(visitor->context,qa_catalog_resources(catalog),e) ||
            !visitor->catalog(visitor->context,catalog,e) ||
            !visitor->view(visitor->context,qa_catalog_files(catalog),e) ||
            !visitor->pool(visitor->context,qa_vfs_resources(held->content),e) ||
            !visitor->view(visitor->context,held->content,e)) return false;
    }
    return true;
}
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if (!qa_source_save_count(io,&count,64u*1024u*1024u)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if (io->offset>io->input.size || count>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool settings_fields(qa_source_save_io *io,qa_cvars **vars,qa_console_dialect dialect)
{
    qa_buffer data={0}; qa_bytes bytes={0}; bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    bool ok=!writing || qa_cvars_save_capture(*vars,&data,io->error);
    if (writing) bytes=(qa_bytes){data.data,data.size};
    if (ok) ok=blob(io,&bytes) && bytes.size;
    if (ok && !writing) {
        *vars=qa_cvars_create(&(qa_cvar_options){.dialect=dialect},io->error);
        qa_cvars_restore *ticket=NULL;
        ok=*vars && qa_cvars_save_prepare(*vars,bytes,&ticket,io->error) && qa_cvars_save_commit(ticket,io->error);
        if (!ok) qa_cvars_save_abort(ticket);
    }
    qa_buffer_free(&data); return ok;
}
static bool row_fields(frontend_neutral_config *row,qa_source_save_io *io,
    const qa_application_content_graph *graph)
{
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    const qa_launch_instance *held=descriptor(row);
    char *name=writing?(char *)held->selection.instance:NULL;
    qa_sha256_digest identity=writing?held->identity:(qa_sha256_digest){0};
    uint32_t dialect=row->dialect,movement=row->kind,logical=writing?row->command.seat:0;
    bool ok=frontend_save_text(io,&name) && name && *name &&
        qa_source_save_bytes(io,&identity,sizeof(identity)) && qa_source_save_u32(io,&dialect) && dialect<=QA_CONSOLE_Q3 &&
        qa_source_save_u32(io,&movement) && movement<=QA_MOVEMENT_Q3 && qa_source_save_u32(io,&logical) &&
        qa_source_save_u32(io,&row->physical_seat) && row->physical_seat<row->owner->frontend->options.seats &&
        qa_source_save_u64(io,&row->namespace_revision) && row->namespace_revision &&
        qa_source_save_bool(io,&row->startup_owned) &&
        qa_source_save_bool(io,&row->movement_selected) && (row->movement_selected || dialect==movement);
    if (!writing) { row->saved_instance=name; row->saved_identity=identity;
        row->saved_seat=logical; row->dialect=(qa_console_dialect)dialect; row->kind=(qa_movement_kind)movement; }
    qa_buffer files={0}; qa_bytes bytes={0};
    if (ok && writing) { ok=frontend_config_files_checkpoint(row->files,graph,&files,io->error);
        bytes=(qa_bytes){files.data,files.size}; }
    if (ok) ok=blob(io,&bytes) && bytes.size;
    if (ok && !writing) ok=frontend_config_files_restore((qa_application_content_graph *)graph,bytes,&row->files,io->error);
    qa_buffer_free(&files);
    if (ok) ok=settings_fields(io,&row->mouse,row->dialect);
    if (ok && row->dialect!=(qa_console_dialect)row->kind)
        ok=settings_fields(io,&row->movement,(qa_console_dialect)row->kind);
    if (ok && !writing) { row->authored=frontend_authored_bindings_create(io->error); ok=row->authored!=NULL; }
    return ok && frontend_authored_bindings_fields(row->authored,io) && frontend_authored_bindings_completed(row->authored);
}
static bool header(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','N','C'}; uint32_t schema=3;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFNC",4) &&
        qa_source_save_u32(io,&schema) && schema==3 && qa_source_save_count(io,count,
            io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX);
}
bool frontend_neutral_configs_checkpoint(const frontend_neutral_configs *owner,
    const qa_application_content_graph *graph,qa_buffer *out,qa_error *e)
{
    if (!owner || owner->restoring || !graph || !out || out->data || out->size)
        return fail(e,QA_ERROR_ARGUMENT,"Neutral checkpoint requires its returned actual roster");
    size_t count=0;
    for (const frontend_neutral_config *row=owner->rows;row;row=row->next) {
        frontend_neutral_config_view view;
        if (!frontend_neutral_config_read(owner,row->source.context.console,&view,e) || !frontend_config_files_idle(row->files)) return false;
        ++count;
    }
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e) && header(&io,&count);
    for (frontend_neutral_config *row=owner->rows;ok && row;row=row->next) ok=row_fields(row,&io,graph);
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_neutral_configs_restore(frontend_neutral_configs *owner,qa_application_content_graph *graph,
    qa_bytes bytes,qa_error *e)
{
    if (!owner || owner->rows || owner->restoring || !graph)
        return fail(e,QA_ERROR_ARGUMENT,"Neutral import requires its empty real roster");
    owner->restoring=true;
    qa_source_save_io io={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && header(&io,&count);
    frontend_neutral_config **at=&owner->rows;
    for (size_t i=0;ok && i<count;++i) {
        frontend_neutral_config *row=calloc(1,sizeof(*row));
        if (!row) { ok=fail(e,QA_ERROR_MEMORY,"Decoding neutral CLIENT configuration"); break; }
        row->owner=owner; row->imported=true; row->ready=row->published=true;
        *at=row; at=&row->next;
        ok=row_fields(row,&io,graph);
        for (frontend_neutral_config *previous=owner->rows;ok && previous!=row;previous=previous->next)
            ok=strcmp(previous->saved_instance,row->saved_instance)!=0 || previous->saved_seat!=row->saved_seat;
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_neutral_configs_finish_restore(frontend_neutral_configs *owner,qa_error *e)
{
    if (!owner || !owner->restoring) return fail(e,QA_ERROR_ARGUMENT,"Neutral finish requires its decoded roster");
    for (frontend_neutral_config *row=owner->rows;row;row=row->next) {
        frontend_neutral_config_view view;
        if (row->imported || !row->issued || !frontend_neutral_config_read(owner,row->source.context.console,&view,e))
            return fail(e,QA_ERROR_FORMAT,"Decoded neutral configuration has no actual CLIENT constructor");
    }
    owner->restoring=false; return true;
}
