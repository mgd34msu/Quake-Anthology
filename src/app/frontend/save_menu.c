#include "save_menu.h"
#include "ui_features_private.h"
#include "save_commands.h"
#include "qa/application_save_policy.h"
#include "qa/application_q1_save.h"
#include "qa/q1_save_product.h"
#include "qa/text.h"
#include "qa/ui_saves.h"
#include <stdio.h>

static char *copy(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *out=malloc(size);
    if (!out) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining source save product text"); return NULL; }
    memcpy(out,text,size); return out;
}
static char *product_label(const qa_product *product,qa_error *error)
{
    const char *title=product->title && *product->title?product->title:product->key;
    size_t a=strlen(title),b=strlen(product->key);
    if (a>SIZE_MAX-4 || b>SIZE_MAX-a-4) {
        frontend_fail(error,QA_ERROR_MEMORY,"Source save product label overflows"); return NULL;
    }
    char *label=malloc(a+b+4);
    if (!label) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining identifiable source save product"); return NULL; }
    memcpy(label,title,a); label[a]=' '; label[a+1]='(';
    memcpy(label+a+2,product->key,b); label[a+b+2]=')'; label[a+b+3]=0; return label;
}
static void clear_products(frontend_ui_seat_features *state)
{
    for (size_t i=0;i<state->save_product_count;++i) {
        free(state->save_product_keys[i]); free(state->save_product_labels[i]);
    }
    free(state->save_product_keys); free(state->save_product_labels);
    state->save_product_keys=state->save_product_labels=NULL;
    state->save_product_count=state->selected_product=0;
}
static bool source_read(frontend_seat *seat,const qa_save_slot_entry *entry,
    qa_q1_save_data **source,char **path,qa_error *error)
{
    qa_save_image *image=NULL;
    qa_fs_root *root=frontend_save_commands_root(seat->frontend);
    bool ok=qa_saved_game_read(root,entry->name,&image,source,error);
    if (ok && (!*source || ((*source)->version==5?QA_SAVE_SLOT_Q1_V5:QA_SAVE_SLOT_Q1_V6)!=entry->format))
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Saved game changed format; refresh its slot");
    if (ok) ok=qa_fs_root_join(root,entry->name,path,error);
    qa_error cleanup={0};
    if (!frontend_save_image_release(seat->frontend,&image,ok?error:&cleanup)) ok=false;
    return ok;
}
static bool select_product(frontend_seat *seat,const qa_q1_save_data *source,const char *path,
    const char *key,const qa_product **out,qa_error *error)
{
    qa_application *application=seat->frontend->application;
    return qa_q1_save_select_product(qa_application_catalog(application),source,path,key,out,error) &&
        qa_application_q1_save_import_ready(application,source,(*out)->key,error);
}
static bool products(frontend_seat *seat,qa_error *error)
{
    frontend_ui_seat_features *state=frontend_ui_features_seat(seat);
    clear_products(state);
    if (state->selected_save>=state->saves.count) return true;
    qa_save_slot_entry *entry=state->saves.entries+state->selected_save;
    if (entry->error.code!=QA_OK || entry->format==QA_SAVE_SLOT_SHARED) return true;
    qa_q1_save_data *source=NULL; char *path=NULL; qa_error local={0};
    const qa_catalog *catalog=qa_application_catalog(seat->frontend->application);
    bool ok=source_read(seat,entry,&source,&path,&local);
    if (!ok) {
        qa_q1_save_destroy(source); free(path);
        if (local.code==QA_ERROR_MEMORY) { if (error) *error=local; return false; }
        state->save_qualification[state->selected_save]=local; return true;
    }
    size_t capacity=qa_catalog_count(catalog);
    if (capacity==SIZE_MAX || ++capacity>SIZE_MAX/sizeof(char *)) {
        qa_q1_save_destroy(source); free(path); return frontend_fail(error,QA_ERROR_MEMORY,"Source save product choices overflow");
    }
    char **keys=calloc(capacity,sizeof(*keys)),**labels=calloc(capacity,sizeof(*labels));
    if (!keys || !labels) {
        free(keys); free(labels); qa_q1_save_destroy(source); free(path);
        return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine source save choices");
    }
    size_t count=1,selected=0;
    labels[0]=copy("Choose source game",error); ok=labels[0]!=NULL;
    const qa_product *automatic=NULL;
    if (!select_product(seat,source,path,NULL,&automatic,&local)) {
        state->save_qualification[state->selected_save]=local;
        if (local.code==QA_ERROR_MEMORY) { if (error) *error=local; ok=false; }
    }
    else state->save_qualification[state->selected_save]=(qa_error){0};
    for (size_t i=0;ok && i<qa_catalog_count(catalog);++i) {
        const qa_product *candidate=qa_catalog_at(catalog,i),*qualified=NULL;
        if (!candidate || !candidate->key || candidate->family!=QA_GAME_Q1 || candidate->availability!=QA_CONTENT_INSTALLED) continue;
        qa_error observed={0};
        if (!select_product(seat,source,path,candidate->key,&qualified,&observed)) {
            if (observed.code==QA_ERROR_MEMORY) { if (error) *error=observed; ok=false; }
            continue;
        }
        keys[count]=copy(qualified->key,error);
        labels[count]=product_label(qualified,error);
        if (!keys[count] || !labels[count]) { ++count; ok=false; break; }
        if (qualified==automatic) selected=count;
        ++count;
    }
    qa_q1_save_destroy(source); free(path);
    if (!ok) {
        for (size_t i=0;i<count;++i) { free(keys[i]); free(labels[i]); }
        free(keys); free(labels); return false;
    }
    if (count==1) { free(labels[0]); free(keys); free(labels); return true; }
    state->save_product_keys=keys; state->save_product_labels=labels;
    state->save_product_count=count; state->selected_product=selected; return true;
}
static bool refresh(frontend_seat *seat, qa_error *error)
{
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    qa_fs_root *root = frontend_save_commands_root(seat->frontend);
    qa_save_slot_listing listing = {0};
    if (!state || !root || !qa_save_slots_list(root, "saves", &listing, error)) return false;
    if (listing.count > SIZE_MAX / sizeof(qa_ui_row) || listing.count > SIZE_MAX / 256 || listing.count>SIZE_MAX/sizeof(qa_error)) {
        qa_save_slot_listing_free(&listing); return frontend_fail(error, QA_ERROR_MEMORY, "Save menu inventory exceeds memory extent");
    }
    qa_ui_row *rows = listing.count ? calloc(listing.count, sizeof(*rows)) : NULL;
    char *details = listing.count ? calloc(listing.count, 256) : NULL;
    qa_error *qualifications=listing.count?calloc(listing.count,sizeof(*qualifications)):NULL;
    if (listing.count && (!rows || !details || !qualifications)) {
        free(rows); free(details); free(qualifications); qa_save_slot_listing_free(&listing);
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining save menu rows");
    }
    for (size_t i=0;i<listing.count;++i) if (listing.entries[i].error.code==QA_OK && listing.entries[i].format!=QA_SAVE_SLOT_SHARED) {
        qa_q1_save_data *source=NULL; char *path=NULL; const qa_product *product=NULL;
        bool ok=source_read(seat,listing.entries+i,&source,&path,qualifications+i) &&
            select_product(seat,source,path,NULL,&product,qualifications+i);
        if(ok)snprintf(details+listing.count*128+i*128,128,"%s",product->title && *product->title?product->title:product->key);
        qa_q1_save_destroy(source); free(path);
        if (!ok && qualifications[i].code==QA_ERROR_MEMORY) {
            if (error) *error=qualifications[i];
            free(rows); free(details); free(qualifications); qa_save_slot_listing_free(&listing); return false;
        }
    }
    clear_products(state);
    qa_save_slot_listing_free(&state->saves); free(state->save_rows); free(state->save_details); free(state->save_qualification);
    state->saves = listing; state->save_rows = rows; state->save_details = details;
    state->save_qualification=qualifications;
    ++state->save_revision;
    if (state->selected_save >= listing.count) state->selected_save = 0;
    state->overwrite = false; state->save_error[0] = 0; return products(seat,error);
}
static bool queue(frontend_seat *seat, bool load, bool overwrite, qa_error *error)
{
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    const char *name = state->save_name ? state->save_name : "";
    const char *product_key=NULL;
    if (load) {
        if (state->selected_save >= state->saves.count || state->saves.entries[state->selected_save].error.code != QA_OK)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Select a verified save image to load");
        name = state->saves.entries[state->selected_save].name + 6;
        qa_save_slot_entry *entry=state->saves.entries+state->selected_save;
        if (entry->format!=QA_SAVE_SLOT_SHARED) {
            if (state->selected_product && state->selected_product<state->save_product_count)
                product_key=state->save_product_keys[state->selected_product];
            qa_q1_save_data *source=NULL; char *path=NULL; const qa_product *product=NULL;
            bool ok=source_read(seat,entry,&source,&path,error) &&
                select_product(seat,source,path,product_key,&product,error);
            qa_q1_save_destroy(source); free(path);
            if (!ok) return false;
        }
    }
    if (!qa_application_save_policy(seat->frontend->application, frontend_network_save_authority(seat->frontend),
        seat->frontend->options.dedicated, load, QA_SAVE_MANUAL, error)) return false;
    if (!load && !overwrite) {
        char *path = frontend_save_commands_slot_path(seat->frontend, name, error);
        if (!path) return false;
        qa_fs_entry_kind kind;
        bool ok = qa_fs_root_status(frontend_save_commands_root(seat->frontend), path, &kind, NULL, error);
        free(path);
        if (!ok) return false;
        if (kind != QA_FS_MISSING) {
            return frontend_fail(error,QA_ERROR_ARGUMENT,"This save exists. Select it from Save game to confirm overwrite.");
        }
    }
    const char *argv[] = {load ? "load" : "save", name,product_key};
    qa_command_invocation invocation = {.console = qa_application_console(seat->frontend->application),
        .context = qa_input_seat_context(seat->input),
        .argc = product_key?3:2, .argv = argv, .args_text = name, .raw = argv[0]};
    invocation.context.dialect=QA_CONSOLE_Q1;
    bool ok = frontend_save_commands_queue(seat->frontend, &invocation, error);
    if (ok) { state->overwrite = false; state->save_error[0] = 0; }
    return ok;
}
struct frontend_save_menu {
    frontend_seat *seat;
    qa_ui_saves *menus;
    qa_ui_save_entry *entries;
    int64_t *saved_at;
    size_t count,recovery_rows;
    char policy[256],listing_error[256];
};
static int newest_first(const void *left,const void *right)
{
    const qa_ui_save_entry *a=left,*b=right;
    if(a->saved_at_ms!=b->saved_at_ms)return a->saved_at_ms>b->saved_at_ms?-1:1;
    return strcmp(a->id,b->id);
}
static bool read_entries(void *context,const qa_ui_save_entry **entries,size_t *count,const char **message,qa_error *error)
{
    frontend_save_menu *owner=context;qa_ui_state state;bool available=false;
    if (!qa_ui_state_read(owner->seat->ui,&state,error) ||
        !frontend_save_commands_recovery_available(owner->seat->frontend,&available,error)) return false;
    size_t first=state.menu==FRONTEND_LOAD && available?0:owner->recovery_rows;
    *entries=owner->entries?owner->entries+first:NULL;
    *count=owner->count-first;*message=owner->listing_error;return true;
}
static bool rebuild_entries(frontend_save_menu *owner,int64_t *saved_at,bool inspect_time,qa_error *error)
{
    frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);
    bool available=false;
    if (!frontend_save_commands_recovery_available(owner->seat->frontend,&available,error)) return false;
    size_t prefix=available?2:0;
    if (state->saves.count>SIZE_MAX/sizeof(qa_ui_save_entry)-prefix)
        return frontend_fail(error,QA_ERROR_MEMORY,"Saved game menu entries exceed memory");
    size_t count=state->saves.count+prefix;
    qa_ui_save_entry *rows=count?calloc(count,sizeof(*rows)):NULL;
    if(count && !rows)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining saved game menu entries");
    if (prefix) {
        rows[0]=(qa_ui_save_entry){.id="recovery:resume",.label="Recover interrupted session",.map="Last completed frame",.game=""};
        rows[1]=(qa_ui_save_entry){.id="recovery:discard",.label="Discard interrupted session",.map="Keep current game",.game=""};
    }
    for(size_t i=0;i<state->saves.count;++i) {
        qa_save_slot_entry *entry=state->saves.entries+i;
        char *label=state->save_details+i*128;
        const char *name=entry->name+6;size_t length=strlen(name);
        if(length>=4 && !strcmp(name+length-4,".sav"))length-=4;
        snprintf(label,128,"%.*s",(int)(length>127?127:length),name);
        if(!strcmp(label,"autosave"))snprintf(label,128,"Autosave");
        else if(!strcmp(label,"quicksave"))snprintf(label,128,"Quicksave");
        else for(char *part=label;*part;++part)if(*part=='_')*part=' ';
        rows[prefix+i]=(qa_ui_save_entry){.id=entry->name,.label=label,
            .map=entry->format==QA_SAVE_SLOT_SHARED?entry->metadata.map:entry->source.map?entry->source.map:"",
            .game=entry->format==QA_SAVE_SLOT_SHARED?entry->metadata.game:state->save_details+state->saves.count*128+i*128,
            .unavailable=entry->error.code!=QA_OK?entry->error.message:state->save_qualification[i].code!=QA_OK?state->save_qualification[i].message:NULL,
            .requires_product=entry->error.code==QA_OK && entry->format!=QA_SAVE_SLOT_SHARED && state->save_qualification[i].code!=QA_OK};
        qa_fs_entry_kind kind;qa_fs_identity identity;qa_fs_timestamp stamp;qa_error timestamp_error={0};
        if(inspect_time && qa_fs_root_status(frontend_save_commands_root(owner->seat->frontend),entry->name,&kind,&identity,&timestamp_error) &&
            kind==QA_FS_REGULAR && qa_fs_identity_modified_time(&identity,&stamp) &&
            stamp.seconds>=0 && stamp.seconds<=(INT64_MAX-(int64_t)(stamp.nanoseconds/1000000))/1000)
            saved_at[i]=stamp.seconds*1000+(int64_t)(stamp.nanoseconds/1000000);
        rows[prefix+i].saved_at_ms=saved_at[i];
    }
    free(owner->entries);free(owner->saved_at);owner->entries=rows;owner->saved_at=saved_at;
    owner->count=count;owner->recovery_rows=prefix;
    if(state->saves.count)qsort(owner->entries+prefix,state->saves.count,sizeof(*owner->entries),newest_first);
    return true;
}
static bool refresh_entries(void *context,qa_error *error)
{
    frontend_save_menu *owner=context;frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);qa_error local={0};
    if(!refresh(owner->seat,&local)) {
        snprintf(owner->listing_error,sizeof(owner->listing_error),"%s",local.message);
        if(local.code==QA_ERROR_MEMORY) { if(error)*error=local;return false; }return true;
    }
    free(owner->entries);free(owner->saved_at);owner->entries=NULL;owner->saved_at=NULL;owner->count=0;
    int64_t *saved_at=state->saves.count?calloc(state->saves.count,sizeof(*saved_at)):NULL;
    if(state->saves.count && !saved_at)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining save file timestamps");
    if(!rebuild_entries(owner,saved_at,true,error)) { free(saved_at);return false; }
    owner->listing_error[0]=0;return true;
}
static const char *unavailable(void *context,bool saving)
{
    frontend_save_menu *owner=context;frontend_seat *seat=owner->seat;qa_error local={0};
    bool ok=qa_application_save_policy(seat->frontend->application,frontend_network_save_authority(seat->frontend),
        seat->frontend->options.dedicated,!saving,QA_SAVE_MANUAL,&local);
    snprintf(owner->policy,sizeof(owner->policy),"%s",ok?"":local.message);return ok?NULL:owner->policy;
}
static bool pending(void *context)
{ return frontend_save_commands_pending(((frontend_save_menu *)context)->seat->frontend); }
static bool write_save(void *context,const char *name,const char *overwrite,qa_error *error)
{
    frontend_save_menu *owner=context;frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);
    if(overwrite) {
        size_t i=0;while(i<state->saves.count && strcmp(overwrite,state->saves.entries[i].name))++i;
        if(i==state->saves.count)return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected saved game is no longer listed");
        name=state->saves.entries[i].name+6;
    }
    char *draft=copy(name,error);if(!draft)return false;
    free(state->save_name);state->save_name=draft;
    return queue(owner->seat,false,overwrite!=NULL,error);
}
static bool load_save(void *context,const char *id,qa_error *error)
{
    frontend_save_menu *owner=context;frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);
    if (!strcmp(id,"recovery:resume") || !strcmp(id,"recovery:discard"))
        return frontend_save_commands_recovery_queue(owner->seat->frontend,!strcmp(id,"recovery:resume"),error);
    size_t i=0;while(i<state->saves.count && strcmp(id,state->saves.entries[i].name))++i;
    if(i==state->saves.count)return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected saved game is no longer listed");
    state->selected_save=i;if(!products(owner->seat,error))return false;
    return queue(owner->seat,true,false,error);
}
static bool product_action(void *context,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    frontend_save_menu *owner=context;frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);
    (void)seat;(void)control;
    if(event->kind!=QA_UI_SELECT)return true;
    if(event->value.row>=state->save_product_count)return frontend_fail(error,QA_ERROR_ARGUMENT,"Source game leaves actual saved game choices");
    state->selected_product=event->value.row;qa_error local={0};
    if(!queue(owner->seat,true,false,&local)) {
        snprintf(owner->listing_error,sizeof(owner->listing_error),"%s",local.message);
        if(local.code==QA_ERROR_MEMORY) { if(error)*error=local;return false; }
    }
    return true;
}
static bool load_choice(void *context,qa_ui_control *control,bool *present,qa_error *error)
{
    frontend_save_menu *owner=context;frontend_ui_seat_features *state=frontend_ui_features_seat(owner->seat);(void)error;
    *present=state->save_product_count>1 && state->selected_save<state->saves.count;
    if(!*present)return true;
    *control=(qa_ui_control){.id=100,.kind=QA_UI_CHOICE,.label="Source game",.rect={64,346,512,28},
        .enabled=!pending(owner),.visible=true,.context=owner,.action=product_action};
    control->value.choice.labels=(const char *const *)state->save_product_labels;
    control->value.choice.count=state->save_product_count;control->value.choice.selected=state->selected_product;return true;
}
bool frontend_save_menu_create(frontend_seat *seat,qa_error *error)
{
    if(!seat || seat->save_menu)return frontend_fail(error,QA_ERROR_ARGUMENT,"Saved game menus require their physical seat owner");
    frontend_save_menu *owner=calloc(1,sizeof(*owner));if(!owner)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining saved game menu service");
    owner->seat=seat;seat->save_menu=owner;
    qa_ui_saves_service service={.context=owner,.list=read_entries,.refresh=refresh_entries,.unavailable=unavailable,
        .busy=pending,.save=write_save,.load=load_save,.load_choice=load_choice};
    return qa_ui_saves_create(seat->ui,(qa_ui_saves_menus){.load=FRONTEND_LOAD,.save=FRONTEND_SAVE,
        .name=FRONTEND_SAVE_NAME,.overwrite=FRONTEND_SAVE_OVERWRITE},&service,&owner->menus,error);
}
bool frontend_save_menu_destroy(frontend_seat *seat,qa_error *error)
{
    frontend_save_menu *owner=seat?seat->save_menu:NULL;if(!owner)return true;
    if(!qa_ui_saves_destroy(&owner->menus,(double)seat->frontend->time_ns/1000000.0,error))return false;
    free(owner->entries);free(owner->saved_at);free(owner);seat->save_menu=NULL;return true;
}
