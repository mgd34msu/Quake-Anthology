#include "save_menu.h"
#include "ui_features_private.h"
#include "save_commands.h"
#include "qa/application_save_policy.h"
#include "qa/application_q1_save.h"
#include "qa/q1_save_product.h"
#include "qa/text.h"
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
    qa_save_image_destroy(image); return ok;
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
    if (listing.count > SIZE_MAX / sizeof(qa_ui_row) || listing.count > SIZE_MAX / 128 || listing.count>SIZE_MAX/sizeof(qa_error)) {
        qa_save_slot_listing_free(&listing); return frontend_fail(error, QA_ERROR_MEMORY, "Save menu inventory exceeds memory extent");
    }
    qa_ui_row *rows = listing.count ? calloc(listing.count, sizeof(*rows)) : NULL;
    char *details = listing.count ? calloc(listing.count, 128) : NULL;
    qa_error *qualifications=listing.count?calloc(listing.count,sizeof(*qualifications)):NULL;
    if (listing.count && (!rows || !details || !qualifications)) {
        free(rows); free(details); free(qualifications); qa_save_slot_listing_free(&listing);
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining save menu rows");
    }
    for (size_t i=0;i<listing.count;++i) if (listing.entries[i].error.code==QA_OK && listing.entries[i].format!=QA_SAVE_SLOT_SHARED) {
        qa_q1_save_data *source=NULL; char *path=NULL; const qa_product *product=NULL;
        bool ok=source_read(seat,listing.entries+i,&source,&path,qualifications+i) &&
            select_product(seat,source,path,NULL,&product,qualifications+i);
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
static bool open_menu(void *context, uint32_t id, qa_error *error)
{ (void)id; return refresh(context, error); }
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
            state->overwrite = true; return true;
        }
    }
    const char *argv[] = {load ? "load" : "save", name,product_key};
    qa_command_invocation invocation = {.console = qa_application_console(seat->frontend->application),
        .context = {.seat = seat->id, .origin = QA_COMMAND_SEAT, .dialect = QA_CONSOLE_Q1, .direct = true},
        .argc = product_key?3:2, .argv = argv, .args_text = name, .raw = argv[0]};
    bool ok = frontend_save_commands_queue(seat->frontend, &invocation, error);
    if (ok) { state->overwrite = false; state->save_error[0] = 0; }
    return ok;
}
static bool action(void *context, uint32_t id, qa_ui_id control, const qa_ui_action *event, qa_error *error)
{
    frontend_seat *seat = context; frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    if (!state || id != seat->id) return frontend_fail(error, QA_ERROR_ARGUMENT, "Save menu lost its physical seat owner");
    if (control == 1 && (event->kind == QA_UI_SELECT || event->kind == QA_UI_ROW_ACTIVATE)) {
        if (event->value.row >= state->saves.count) return frontend_fail(error, QA_ERROR_ARGUMENT, "Save row is outside its actual inventory");
        state->selected_save = event->value.row; state->overwrite = false;
        if (!products(seat,error)) return false;
        if (event->kind != QA_UI_ROW_ACTIVATE) return true;
    } else if (control==9 && event->kind==QA_UI_SELECT) {
        if (event->value.row>=state->save_product_count)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source save product is outside its actual choices");
        state->selected_product=event->value.row; state->save_error[0]=0;
        qa_save_slot_entry *entry=state->saves.entries+state->selected_save;
        qa_q1_save_data *source=NULL; char *path=NULL; const qa_product *product=NULL; qa_error local={0};
        bool ok=source_read(seat,entry,&source,&path,&local) &&
            select_product(seat,source,path,
                state->save_product_keys[state->selected_product],&product,&local);
        qa_q1_save_destroy(source); free(path);
        if (!ok && local.code==QA_ERROR_MEMORY) { if (error) *error=local; return false; }
        state->save_qualification[state->selected_save]=ok?(qa_error){0}:local; return true;
    } else if (control == 2 && event->kind == QA_UI_CHANGE_TEXT) {
        const char *text = event->value.text ? event->value.text : "";
        size_t size = strlen(text) + 1; char *copy = malloc(size);
        if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining save name draft");
        memcpy(copy, text, size); free(state->save_name); state->save_name = copy;
        state->overwrite = false; state->save_error[0] = 0; return true;
    } else if (event->kind != QA_UI_ACTIVATE && !(control == 2 && event->kind == QA_UI_SUBMIT)) return true;
    qa_error local = {0}; bool ok = true;
    if (control == 1 || control == 4) ok = queue(seat, true, false, &local);
    else if (control == 2 || control == 3) ok = queue(seat, false, false, &local);
    else if (control == 5) ok = refresh(seat, &local);
    else if (control == 6 && state->overwrite) ok = queue(seat, false, true, &local);
    else if (control == 7) state->overwrite = false;
    if (!ok) snprintf(state->save_error, sizeof(state->save_error), "%s", local.message);
    return true;
}
static bool menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; frontend_ui_seat_features *state = frontend_ui_features_seat(seat); (void)id;
    if (!state || (state->saves.count && (!state->save_rows || !state->save_details || !state->save_qualification)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Save menu has no actual retained inventory");
    bool pending = frontend_save_commands_pending(seat->frontend);
    for (size_t i = 0; i < state->saves.count; ++i) {
        qa_save_slot_entry *entry = &state->saves.entries[i]; char *detail = state->save_details + i * 128;
        if (entry->error.code != QA_OK) snprintf(detail, 128, "Unreadable save");
        else if (entry->format==QA_SAVE_SLOT_SHARED) snprintf(detail, 128, "%llu:%02llu elapsed", (unsigned long long)(entry->metadata.elapsed_ns / UINT64_C(60000000000)),
            (unsigned long long)(entry->metadata.elapsed_ns / UINT64_C(1000000000) % 60));
        else if (state->save_qualification[i].code!=QA_OK) snprintf(detail,128,"%s",state->save_qualification[i].message);
        else { char time[32]; if (!qa_format_number(entry->source.time,time,error)) return false;
            snprintf(detail,128,"%s · %ss · kills %s/%s · secrets %s/%s",entry->source.map,time,
                entry->source.killed_monsters?entry->source.killed_monsters:"?",
                entry->source.total_monsters?entry->source.total_monsters:"?",
                entry->source.found_secrets?entry->source.found_secrets:"?",
                entry->source.total_secrets?entry->source.total_secrets:"?"); }
        const char *label=entry->format!=QA_SAVE_SLOT_SHARED && entry->source.comment && *entry->source.comment?entry->source.comment:entry->name+6;
        state->save_rows[i] = (qa_ui_row){.key = entry->name, .label = label, .detail = detail, .enabled = entry->error.code == QA_OK};
    }
    const char *labels[] = {"", "Save name", "Save", "Load selected", "Refresh", "Confirm overwrite", "Cancel overwrite",
        *state->save_error ? state->save_error : state->overwrite ? "This slot exists. Confirm overwrite to replace it." : pending ? "Save/load request pending" : "Choose a save to load, or enter a name to save."};
    for (unsigned i = 0; i < 8; ++i) seat->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
        .label = labels[i], .rect = {40 + (float)(i % 2) * 280, 292 + (float)(i / 2) * 34, 270, 28},
        .enabled = !pending, .visible = true, .context = seat, .action = action};
    seat->controls[0].kind = QA_UI_LIST; seat->controls[0].rect = (qa_scene_rect_f){40, 80, 560, 200};
    seat->controls[0].value.list.rows = state->save_rows; seat->controls[0].value.list.count = state->saves.count;
    seat->controls[0].value.list.selected = state->selected_save; seat->controls[0].value.list.row_height = 24;
    seat->controls[0].value.list.revision = state->save_revision;
    seat->controls[1].kind = QA_UI_FIELD; seat->controls[1].rect = (qa_scene_rect_f){40, 288, 560, 28};
    seat->controls[1].value.field.text = state->save_name ? state->save_name : ""; seat->controls[1].value.field.maximum = 255;
    seat->controls[2].rect = (qa_scene_rect_f){40, 326, 170, 28}; seat->controls[3].rect = (qa_scene_rect_f){225, 326, 190, 28};
    seat->controls[4].rect = (qa_scene_rect_f){430, 326, 170, 28};
    seat->controls[3].enabled = !pending && state->selected_save < state->saves.count &&
        state->saves.entries[state->selected_save].error.code == QA_OK && state->save_qualification[state->selected_save].code==QA_OK;
    seat->controls[5].visible = seat->controls[6].visible = state->overwrite;
    seat->controls[5].rect=(qa_scene_rect_f){40,370,270,28};
    seat->controls[6].rect=(qa_scene_rect_f){330,370,270,28};
    seat->controls[7].enabled = false; seat->controls[7].rect = (qa_scene_rect_f){40, 412, 560, 40};
    seat->controls[8]=(qa_ui_control){.id=9,.kind=QA_UI_CHOICE,.label="Source game",.rect={40,370,560,28},
        .enabled=!pending && state->save_product_count>1,.visible=!state->overwrite && state->save_product_count>1,.context=seat,.action=action};
    seat->controls[8].value.choice.labels=(const char *const *)state->save_product_labels;
    seat->controls[8].value.choice.count=state->save_product_count; seat->controls[8].value.choice.selected=state->selected_product;
    *out = (qa_ui_menu){.id = FRONTEND_SAVES, .title = "Save / load", .controls = seat->controls, .count = 9, .fullscreen = true}; return true;
}
bool frontend_save_menu_create(frontend_seat *seat, qa_error *error)
{
    return seat && qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_SAVES,
        .context = seat, .factory = menu, .open = open_menu}, error);
}
