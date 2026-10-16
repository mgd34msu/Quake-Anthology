#include "inventory_menu.h"
#include "ui_features_private.h"
#include "qa/material.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INVENTORY_MENU=262 };
struct frontend_inventory_menu {
    frontend_seat *seat;
    void *source;
    frontend_inventory_menu_reader read;
    qa_actor_id actor;
    qa_item_id selected,source_selected;
    uint64_t revision;
    size_t row_count;
    bool source_visible,visible,pending;
    qa_ui_control controls[3];
};
static frontend_inventory_menu *menu(frontend_seat *seat)
{
    frontend_ui_seat_features *features=frontend_ui_features_seat(seat);
    return features?features->inventory:NULL;
}
static bool command(frontend_inventory_menu *owner,const char *argument,qa_error *error)
{
    frontend_seat *seat=owner->seat;qa_frontend *f=seat->frontend;
    if(f->shutdown || qa_application_should_stop(f->application)) return true;
    qa_console *console;qa_cvars *cvars;qa_command_context input,captured;
    if(!qa_input_seat_recipient_read(seat->input,&console,&cvars,&input)) return true;
    if(!qa_console_cvar_context(console,&input,&captured,error)) return false;
    size_t size=argument?strlen(argument):0;
    if(argument && strpbrk(argument,"\"\r\n")) {
        frontend_print(f,"Inventory item name cannot be represented by the selected console protocol.");
        return true;
    }
    char *text=qa_arena_alloc(&f->frame.storage,size+12,1,error);
    if(!text) return false;
    int written=argument?snprintf(text,size+12,"use \"%s\"\n",argument):snprintf(text,size+12,"putaway\n");
    frontend_queued_command queued={.console=console,.context=captured,
        .time_ns=f->time_ns,.frame_number=f->frame_number};
    qa_platform_event_result result=qa_platform_events_push(f->platform_events,
        &(qa_sys_event){.kind=QA_PLATFORM_EVENT_CONSOLE_COMMAND,.time_ns=f->wall_time_ns},
        (qa_bytes){(const uint8_t *)&queued,sizeof(queued)},
        (qa_bytes){(const uint8_t *)text,(size_t)written+1});
    if(result!=QA_PLATFORM_EVENT_ACCEPTED)
        frontend_print(f,"Input queue is full; inventory command was not applied.");
    return true;
}
static bool read_items(frontend_inventory_menu *owner,qa_hud_data *data,qa_strings **strings,qa_error *error)
{
    *data=(qa_hud_data){0};*strings=NULL;
    return !owner->read || owner->read(owner->source,data,strings,error);
}
static bool action(void *context,uint32_t physical,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    frontend_inventory_menu *owner=context;
    if(physical!=owner->seat->id) return true;
    if(control==3 && event->kind==QA_UI_ACTIVATE)
        return qa_ui_close(owner->seat->ui,(double)owner->seat->frontend->time_ns/1000000.,error);
    qa_hud_data data;qa_strings *strings;
    if(!read_items(owner,&data,&strings,error)) return false;
    if(control==1 && (event->kind==QA_UI_SELECT || event->kind==QA_UI_ROW_ACTIVATE)) {
        if(event->value.row>=data.inventory_count) return true;
        owner->selected=data.inventory[event->value.row].item;
        if(event->kind!=QA_UI_ROW_ACTIVATE) return true;
    } else if(control!=2 || event->kind!=QA_UI_ACTIVATE) return true;
    const char *id=qa_strings_cstr(strings,owner->selected);
    return !id || command(owner,id,error);
}
static const qa_scene_image *icon(const qa_material *material)
{
    return material && material->stage_count && material->stages[0].image_count?
        material->stages[0].images[0]:NULL;
}
static bool factory(void *context,uint32_t physical,qa_ui_menu *out,qa_error *error)
{
    frontend_inventory_menu *owner=context;qa_hud_data data;qa_strings *strings;
    (void)physical;
    if(!read_items(owner,&data,&strings,error)) return false;
    qa_arena *storage=&owner->seat->frontend->frame.storage;
    size_t count=data.inventory_count,selected=SIZE_MAX;
    qa_ui_row *rows=count?qa_arena_alloc(storage,count*sizeof(*rows),_Alignof(qa_ui_row),error):NULL;
    if(count && !rows) return false;
    for(size_t i=0;i<count;++i) {
        const qa_hud_inventory_row *item=data.inventory+i;
        char number[64];snprintf(number,sizeof(number),"%.17g",item->count);
        const char *detail=qa_arena_text(storage,number,error);
        if(!detail) return false;
        rows[i]=(qa_ui_row){.key=qa_strings_cstr(strings,item->item),.label=item->label,
            .detail=detail,.image=icon(item->icon),.enabled=item->count>0};
        if(item->item==owner->selected) selected=i;
    }
    if(selected==SIZE_MAX && count) {
        selected=0;
        for(size_t i=0;i<count;++i) if(data.inventory[i].selected) { selected=i;break; }
        owner->selected=data.inventory[selected].item;
    } else if(!count) owner->selected=QA_STRING_NONE;
    if(count!=owner->row_count) { owner->row_count=count;++owner->revision; }
    owner->controls[0]=(qa_ui_control){.id=1,.kind=QA_UI_LIST,.label="Items",
        .rect={64,96,512,264},.enabled=true,.visible=true,.context=owner,.action=action,
        .value.list={.rows=rows,.count=count,.selected=selected==SIZE_MAX?0:selected,
            .row_height=33,.revision=owner->revision}};
    owner->controls[1]=(qa_ui_control){.id=2,.kind=QA_UI_BUTTON,.label="Use selected item",
        .rect={64,376,512,32},.enabled=owner->selected!=QA_STRING_NONE,.visible=true,.context=owner,.action=action};
    owner->controls[2]=(qa_ui_control){.id=3,.kind=QA_UI_BUTTON,.label="Close",
        .rect={64,424,512,32},.enabled=true,.visible=true,.context=owner,.action=action};
    *out=(qa_ui_menu){.id=INVENTORY_MENU,.title="Inventory",.controls=owner->controls,.count=3};
    return true;
}
static void closed(void *context,uint32_t physical)
{
    frontend_inventory_menu *owner=context;(void)physical;
    bool notify=owner->visible && owner->source_visible;
    owner->visible=false;
    qa_error error={0};
    if(notify && !command(owner,NULL,&error)) frontend_print(owner->seat->frontend,error.message);
}
bool frontend_inventory_menu_create(frontend_seat *seat,qa_error *error)
{
    frontend_ui_seat_features *features=frontend_ui_features_seat(seat);
    if(!features) return false;
    frontend_inventory_menu *owner=features->inventory;
    if(owner) *owner=(frontend_inventory_menu){0};
    else owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the inventory menu");
    owner->seat=seat;
    if(!qa_ui_register(seat->ui,&(qa_ui_menu_registration){INVENTORY_MENU,owner,factory,NULL,closed},error)) {
        if(!features->inventory) free(owner);
        return false;
    }
    features->inventory=owner;return true;
}
void frontend_inventory_menu_destroy(frontend_inventory_menu **slot)
{ if(slot) { free(*slot);*slot=NULL; } }
void frontend_inventory_menu_source(frontend_seat *seat,void *source,frontend_inventory_menu_reader read,bool visible)
{
    frontend_inventory_menu *owner=menu(seat);
    if(!owner) return;
    if(owner->source!=source || owner->read!=read) {
        owner->source=source;owner->read=read;owner->selected=QA_STRING_NONE;
        owner->source_visible=!visible;++owner->revision;
    }
    if(owner->source_visible!=visible) {
        owner->source_visible=visible;owner->visible=visible;owner->pending=true;
    }
}
void frontend_inventory_menu_clear(frontend_seat *seat,const void *source)
{
    frontend_inventory_menu *owner=menu(seat);
    if(owner && (!source || owner->source==source)) {
        owner->source=NULL;owner->read=NULL;owner->source_visible=owner->visible=false;
        owner->selected=QA_STRING_NONE;owner->pending=true;
    }
}
static bool player_items(void *context,qa_hud_data *out,qa_strings **strings,qa_error *error)
{
    frontend_seat *seat=context;frontend_inventory_menu *owner=menu(seat);
    *strings=qa_session_strings(qa_application_session(seat->frontend->application));
    return frontend_inventory_rows_read(seat,owner->actor,owner->source_selected,out,error);
}
bool frontend_inventory_rows_read(frontend_seat *seat,qa_actor_id actor,qa_item_id selected,
    qa_hud_data *out,qa_error *error)
{
    qa_inventory *inventory=qa_application_inventory(seat->frontend->application);
    if(!qa_inventory_has(inventory,actor)) return true;
    qa_arena lease;qa_inventory_entry *entries;size_t count;
    if(!qa_inventory_entries_snapshot(inventory,actor,&lease,&entries,&count,NULL,NULL,error)) return false;
    bool ok=frontend_hud_inventory_rows(seat,&(qa_hud_frame){.actor=actor},entries,count,
        selected,NULL,qa_session_strings(qa_application_session(seat->frontend->application)),out,error);
    qa_inventory_snapshot_release(inventory,&lease);return ok;
}
void frontend_inventory_menu_player(frontend_seat *seat,qa_actor_id actor,qa_item_id selected,bool visible)
{
    frontend_inventory_menu *owner=menu(seat);
    if(!owner) return;
    if(!qa_actor_id_equal(owner->actor,actor)) { frontend_inventory_menu_clear(seat,seat);owner->actor=actor; }
    owner->source_selected=selected;
    frontend_inventory_menu_source(seat,seat,player_items,visible);
}
bool frontend_inventory_menu_sync(frontend_seat *seat,qa_error *error)
{
    frontend_inventory_menu *owner=menu(seat);
    if(!owner || !owner->pending) return true;
    if(owner->visible && qa_input_seat_focus(seat->input)==QA_INPUT_CONSOLE) return true;
    qa_ui_state state;
    if(!qa_ui_state_read(seat->ui,&state,error)) return false;
    double now=(double)seat->frontend->time_ns/1000000.;
    bool ok=owner->visible?(qa_ui_menu_opened(seat->ui,INVENTORY_MENU) ||
        qa_ui_open(seat->ui,INVENTORY_MENU,now,error)):
        state.menu!=INVENTORY_MENU || qa_ui_close(seat->ui,now,error);
    if(ok) owner->pending=false;
    return ok;
}
bool frontend_inventory_menu_wire_rows(frontend_seat *seat,const qa_hud_q2_names *names,
    const int32_t *counts,size_t count,int32_t selected,qa_hud_data *out,qa_error *error)
{
    qa_hud_inventory_row *rows=count?qa_arena_alloc(&seat->frontend->frame.storage,
        count*sizeof(*rows),_Alignof(qa_hud_inventory_row),error):NULL;
    if(count && !rows) return false;
    size_t used=0;
    for(size_t i=0;i<count && i<names->layout.max_configs-names->layout.items;++i) {
        if(counts[i]<=0) continue;
        qa_string_id name=names->values[names->layout.items+i];
        const char *label=qa_strings_cstr(names->strings,name);
        if(!label || !*label) continue;
        rows[used++]=(qa_hud_inventory_row){.item=name,.label=label,.count=counts[i],.selected=(int32_t)i==selected};
    }
    out->inventory=rows;out->inventory_count=used;out->inventory_present=true;return true;
}
