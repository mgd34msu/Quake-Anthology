#include "internal.h"
#include "config_store.h"
#include "neutral_config.h"
#include "config_weapon_defaults.h"
#include "qa/ui_library.h"
#include "qa/text.h"
#include "settings_devices.h"
#include <stdio.h>

typedef struct binding_record {
    const char *cells[2], *command, *canonical;
    qa_physical_input input;
    bool bound;
    const qa_input_weapon_binding *items;
    size_t item_count;
} binding_record;
typedef struct binding_action {
    const char *label, *command;
} binding_action;
static const binding_action shared_actions[] = {
    {"Move forward", "+forward"}, {"Move back", "+back"},
    {"Strafe left", "+moveleft"}, {"Strafe right", "+moveright"},
    {"Jump / swim up", "+jump"}, {"Crouch / swim down", "+movedown"},
    {"Walk / run modifier", "+speed"}, {"Turn left", "+left"},
    {"Turn right", "+right"}, {"Look up", "+lookup"}, {"Look down", "+lookdown"},
    {"Fire primary weapon", "+attack"}, {"Use / activate", "+use"},
    {"Next weapon", "weapnext"}, {"Previous weapon", "weapprev"},
    {"Weapon wheel", "+weaponwheel"}, {"Powerup wheel", "+powerupwheel"},
    {"Toggle console", "toggleconsole"}
};
static bool same_input(qa_physical_input a, qa_physical_input b)
{ return a.kind == b.kind && a.device == b.device && a.code == b.code && a.positive == b.positive; }
static bool matches(const char *command, const char *canonical)
{
    return !strcmp(command, canonical) ||
        (!strcmp(canonical, "+weaponwheel") && !strcmp(command, "+wheel")) ||
        (!strcmp(canonical, "+powerupwheel") && !strcmp(command, "+wheel2")) ||
        ((!strcmp(canonical, "+scores") || !strcmp(canonical, "score")) &&
            (!strcmp(command, "+scores") || !strcmp(command, "+showscores")));
}
static const char *weapon_item(const char *command,const qa_input_weapon_binding *items,size_t count)
{
    if (!count || strpbrk(command,";\r\n\\") || strstr(command,"//") || strstr(command,"/*")) return NULL;
    qa_command_tokens tokens={0};
    if (!qa_command_tokenize(command,QA_CONSOLE_Q3,false,&tokens,NULL)) return NULL;
    const qa_input_weapon_binding *item=NULL;
    if (tokens.count==2) item=qa_input_weapon_resolve(tokens.values[0],tokens.values[1],items,count);
    else if (tokens.count>2) {
        size_t size=1;
        for (size_t i=1;i<tokens.count;++i) size+=strlen(tokens.values[i])+1;
        char *argument=malloc(size);
        if (argument) {
            argument[0]=0;
            for (size_t i=1;i<tokens.count;++i) { if(i>1) strcat(argument," "); strcat(argument,tokens.values[i]); }
            item=qa_input_weapon_resolve(tokens.values[0],argument,items,count); free(argument);
        }
    }
    const char *id=item?item->id:NULL; qa_command_tokens_free(&tokens); return id;
}
static bool action_matches(const char *command,const char *canonical,const char *item)
{ return matches(command,canonical) || (item && !strncmp(canonical,"use ",4) && !strcmp(item,canonical+4)); }
static bool record_matches(const binding_record *row,const char *command)
{ return action_matches(command,row->canonical,weapon_item(command,row->items,row->item_count)); }
static bool set_selected(frontend_seat *seat, const char *key, qa_error *error)
{
    char *copy = NULL;
    if (key) {
        size_t size = strlen(key) + 1;
        copy = malloc(size);
        if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected binding");
        memcpy(copy, key, size);
    }
    free(seat->binding_selected); seat->binding_selected = copy;
    return true;
}
static const binding_record *selected_record(const frontend_seat *seat)
{
    if (!seat->binding_selected || !seat->binding_rows || !seat->binding_labels) return NULL;
    for (size_t i = 0; i < seat->binding_capacity; ++i)
        if (seat->binding_rows[i].key && !strcmp(seat->binding_rows[i].key, seat->binding_selected))
            return (const binding_record *)seat->binding_labels + i;
    return NULL;
}
static double now_ms(const frontend_seat *seat)
{ return (double)seat->frontend->time_ns / 1000000.0; }

static const char *binding_text(const qa_input_binding *binding)
{
    return binding->kind == QA_BIND_COMMAND ? binding->command : qa_input_action_command(binding->action);
}
static const char *target_label(const frontend_seat *seat, const char *command)
{
    if (seat->binding_rows && seat->binding_labels) for (size_t i=0;i<seat->binding_capacity;++i) {
        if (!seat->binding_rows[i].enabled) continue;
        const binding_record *row=(const binding_record *)seat->binding_labels+i;
        if (record_matches(row,command)) return row->cells[0];
    }
    return command;
}
static bool set_command(frontend_seat *seat, const char *text, qa_error *error)
{
    if (!text) text = "";
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "editing binding command");
    memcpy(copy, text, size); free(seat->binding_command); seat->binding_command = copy;
    return true;
}
static bool reset_available(const frontend_seat *seat)
{
    qa_console *console=NULL; qa_cvars *cvars=NULL; qa_command_context command;
    if(!seat->frontend->config_store || !qa_input_seat_recipient_read(seat->input,&console,&cvars,&command)) return false;
    if(frontend_config_store_source_context(seat->frontend->config_store,&command)!=NULL ||
        frontend_config_store_client_context(seat->frontend->config_store,&command)!=NULL) return true;
    frontend_neutral_config_view view;
    if(frontend_config_store_neutral_read(seat->frontend->config_store,cvars,&view,NULL))
        return view.physical_seat==seat->id && view.ready && view.published && frontend_neutral_config_current(&view);
    return false;
}
static bool bind_pending(frontend_seat *seat, qa_error *error)
{
    char *selected=NULL;
    if(seat->binding_selected) {
        size_t length=strlen(seat->binding_selected);
        char previous[80]; snprintf(previous,sizeof(previous),":%u:%d:%u:%u",(unsigned)seat->binding_previous.kind,
            seat->binding_previous.device,seat->binding_previous.code,(unsigned)seat->binding_previous.positive);
        size_t suffix=strlen(previous);
        if(qa_input_physical_valid(seat->binding_previous) && length>=suffix && !strcmp(seat->binding_selected+length-suffix,previous)) length-=suffix;
        selected=malloc(length+80);
        if(!selected) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining captured binding selection");
        memcpy(selected,seat->binding_selected,length);
        snprintf(selected+length,80,":%u:%d:%u:%u",(unsigned)seat->pending_binding.kind,
            seat->pending_binding.device,seat->pending_binding.code,(unsigned)seat->pending_binding.positive);
    }
    qa_input_binding binding = {.input = seat->pending_binding, .kind = QA_BIND_COMMAND,
        .command = seat->binding_command};
    if (!qa_input_seat_bind(seat->input, &binding, error)) { free(selected); return false; }
    if (seat->binding_reassign && !same_input(seat->binding_previous, seat->pending_binding))
        (void)qa_input_seat_unbind(seat->input, seat->binding_previous);
    free(seat->binding_selected); seat->binding_selected=selected;
    seat->binding_conflict = false; seat->binding_reassign = false;
    snprintf(seat->binding_status, sizeof(seat->binding_status), "Binding saved");
    return frontend_settings_devices_save(seat->frontend,error);
}
bool frontend_binding_capture(void *context, uint32_t id, qa_physical_input input, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    if (!seat->binding_command || !*seat->binding_command)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "enter a command before capturing a key");
    seat->pending_binding = input;
    const qa_input_binding *old = qa_input_seat_binding(seat->input, input);
    const char *command = old ? binding_text(old) : NULL;
    const binding_record *selected=selected_record(seat);
    if (command && !(selected?record_matches(selected,command):matches(command,seat->binding_command))) {
        char physical[128];
        if (!qa_input_physical_name(input, physical, sizeof(physical)))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "captured key has no physical name");
        seat->binding_conflict = true;
        snprintf(seat->setting_value,sizeof(seat->setting_value),"Currently: %s",target_label(seat,command));
        snprintf(seat->binding_status,sizeof(seat->binding_status),"Replace with: %s",target_label(seat,seat->binding_command));
        return qa_ui_open(seat->ui, FRONTEND_BINDINGS_CONFLICT, now_ms(seat), error);
    }
    return bind_pending(seat, error);
}
void frontend_binding_cancel(void *context, uint32_t id)
{
    frontend_seat *seat = context; (void)id;
    seat->binding_conflict = false; seat->binding_reassign = false; seat->binding_status[0] = 0;
}
static bool capture(frontend_seat *seat, const binding_record *row, bool add, qa_error *error)
{
    if (!row) return true;
    if (!set_command(seat, add ? row->canonical : row->command, error)) return false;
    seat->binding_previous = row->input; seat->binding_reassign = row->bound && !add;
    seat->binding_conflict = false;
    return qa_ui_capture_binding(seat->ui, true, error);
}
static bool action(void *context, uint32_t id, qa_ui_id control, const qa_ui_action *event, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    if (control == 2 && event->kind == QA_UI_CHANGE_TEXT) {
        snprintf(seat->binding_query, sizeof(seat->binding_query), "%s", event->value.text ? event->value.text : "");
        return set_selected(seat, NULL, error);
    }
    if (control == 1 && (event->kind == QA_UI_SELECT || event->kind == QA_UI_ROW_ACTIVATE || event->kind == QA_UI_ROW_DELETE)) {
        size_t index = event->value.row;
        if (index >= seat->binding_capacity || !seat->binding_rows[index].enabled) return true;
        if (!set_selected(seat, seat->binding_rows[index].key, error)) return false;
        seat->selected_binding = index;
        const binding_record *row = (const binding_record *)seat->binding_labels + index;
        if (event->kind == QA_UI_ROW_ACTIVATE) return capture(seat, row, false, error);
        if (event->kind == QA_UI_ROW_DELETE && row->bound) {
            (void)qa_input_seat_unbind(seat->input, row->input);
            return frontend_settings_devices_save(seat->frontend,error);
        }
        return true;
    }
    if (event->kind != QA_UI_ACTIVATE) return true;
    const binding_record *row = selected_record(seat);
    switch (control) {
    case 3: return capture(seat, row, true, error);
    case 4:
        if (row && row->bound) {
            (void)qa_input_seat_unbind(seat->input, row->input);
            return frontend_settings_devices_save(seat->frontend,error);
        }
        return true;
    case 5:
        if (row) {
            size_t i = 0;
            while (i < qa_input_seat_binding_count(seat->input)) {
                const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
                if (record_matches(row,binding_text(binding))) (void)qa_input_seat_unbind(seat->input, binding->input);
                else ++i;
            }
            return frontend_settings_devices_save(seat->frontend,error);
        }
        return true;
    case 7: return qa_ui_open(seat->ui, FRONTEND_BINDINGS_RESET, now_ms(seat), error);
    case 8: return qa_ui_close(seat->ui, now_ms(seat), error);
    default: return true;
    }
}
static bool confirmation_action(void *context, uint32_t id, qa_ui_id control,
    const qa_ui_action *event, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    if (event->kind != QA_UI_ACTIVATE) return true;
    if (control == 1) return qa_ui_close(seat->ui, now_ms(seat), error);
    qa_ui_state state;
    if (!qa_ui_state_read(seat->ui, &state, error)) return false;
    if (state.menu == FRONTEND_BINDINGS_CONFLICT) {
        if (!seat->binding_conflict || !bind_pending(seat, error)) return false;
    } else if (state.menu == FRONTEND_BINDINGS_RESET) {
        uint32_t logical = qa_input_seat_context(seat->input).seat;
        (void)frontend_seat_launch_id_read(seat->frontend,seat->id,&logical);
        int32_t controller = qa_input_platform_controller(seat->frontend->input, seat->id);
        if (!frontend_config_store_reset_bindings(seat->frontend->config_store,logical,controller,error)) return false;
        seat->selected_binding = 0;
        if (!set_selected(seat, NULL, error) || !frontend_settings_devices_save(seat->frontend,error)) return false;
    }
    return qa_ui_close(seat->ui, now_ms(seat), error);
}
static bool confirmation(frontend_seat *seat, bool conflict, qa_ui_menu *out, qa_error *error)
{
    if (conflict && !seat->binding_conflict) return frontend_fail(error,QA_ERROR_ARGUMENT,"No pending binding conflict");
    size_t count = conflict ? 4 : 2;
    for (size_t i = 0; i < count; ++i)
        seat->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
            .rect = {64, 92 + (float)(i + 3) * 28, 512, 28}, .enabled = true,
            .visible = true, .context = seat, .action = confirmation_action};
    if (conflict) {
        if (seat->binding_label_capacity < 256) {
            char *labels = realloc(seat->binding_labels, 256);
            if (!labels) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining binding conflict title");
            seat->binding_labels = labels; seat->binding_label_capacity = 256;
        }
        seat->controls[0].id = 3; seat->controls[0].label = seat->setting_value;
        seat->controls[0].rect.y = 120; seat->controls[0].enabled = false;
        seat->controls[1].id = 4; seat->controls[1].label = seat->binding_status;
        seat->controls[1].rect.y = 148; seat->controls[1].enabled = false;
        seat->controls[2].id = 1; seat->controls[2].label = "Cancel"; seat->controls[2].rect.y = 204;
        seat->controls[3].id = 2; seat->controls[3].label = "Replace binding"; seat->controls[3].rect.y = 232;
        if (!qa_input_physical_name(seat->pending_binding, seat->binding_labels, seat->binding_label_capacity))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Conflicting binding has no physical name");
        size_t length = strlen(seat->binding_labels);
        snprintf(seat->binding_labels + length, seat->binding_label_capacity - length, " is already bound");
    } else {
        seat->controls[0].label = "Keep bindings";
        seat->controls[1].label = "Restore defaults";
        seat->controls[1].enabled=reset_available(seat);
    }
    *out = (qa_ui_menu){.id = conflict ? FRONTEND_BINDINGS_CONFLICT : FRONTEND_BINDINGS_RESET,
        .title = conflict ? seat->binding_labels : "Restore this player's default bindings?",
        .controls = seat->controls, .count = count};
    return true;
}
static bool conflict_menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{ (void)id; return confirmation(context, true, out, error); }
static bool reset_menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{ (void)id; return confirmation(context, false, out, error); }
static bool action_matches_query(frontend_seat *seat, binding_action action, const char *const *resolved, bool *out, qa_error *error)
{
    *out = true;
    if (!*seat->binding_query) return true;
    size_t size = strlen(action.label) + 1;
    size_t bindings = qa_input_seat_binding_count(seat->input);
    for (size_t i = 0; i < bindings; ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
        if (action_matches(binding_text(binding), action.command,resolved[i])) size += 129;
    }
    char *search = malloc(size);
    if (!search) return frontend_fail(error, QA_ERROR_MEMORY, "Searching binding actions");
    snprintf(search, size, "%s", action.label);
    for (size_t i = 0; i < bindings; ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
        if (!action_matches(binding_text(binding), action.command,resolved[i])) continue;
        size_t length = strlen(search); search[length++] = ' ';
        if (!qa_input_physical_name(binding->input, search + length, size - length)) {
            free(search); return frontend_fail(error, QA_ERROR_ARGUMENT, "Binding has no physical name");
        }
    }
    qa_buffer query = {0}, text = {0};
    bool ok = qa_utf8_lower((qa_bytes){(const uint8_t *)seat->binding_query, strlen(seat->binding_query)}, &query, error) &&
        qa_utf8_lower((qa_bytes){(const uint8_t *)search, strlen(search)}, &text, error);
    free(search);
    if (ok) {
        size_t cursor = 0, start = 0;
        while (cursor < query.size) {
            size_t previous = cursor; uint32_t scalar;
            (void)qa_utf8_next((qa_bytes){query.data, query.size}, &cursor, &scalar);
            if (!qa_unicode_whitespace(scalar)) continue;
            query.data[previous] = 0;
            if (previous > start && !strstr((const char *)text.data, (const char *)query.data + start)) *out = false;
            start = cursor;
        }
        if (start < query.size && !strstr((const char *)text.data, (const char *)query.data + start)) *out = false;
    }
    qa_buffer_free(&query); qa_buffer_free(&text); return ok;
}
static char *retain_text(char **cursor, const char *text)
{
    char *out = *cursor; size_t size = strlen(text) + 1;
    memcpy(out, text, size); *cursor += size; return out;
}
static bool menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    qa_console_dialect dialect = qa_input_seat_context(seat->input).dialect;
    const qa_launch_choices *choices = seat->library ? qa_ui_library_choices(seat->library) : NULL;
    const qa_catalog *catalog = seat->library ? qa_ui_library_catalog(seat->library) : NULL;
    qa_game_family entities_family=dialect==QA_CONSOLE_Q1 || dialect==QA_CONSOLE_QW?QA_GAME_Q1:
        dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE?QA_GAME_Q2:QA_GAME_Q3;
    if (choices && catalog) {
        qa_launch_scope scope={.kind=QA_SCOPE_DEFAULT_PLAYER};
        if (seat->id<choices->seat_count) scope=(qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=choices->seats[seat->id].id};
        const qa_launch_binding *movement=qa_launch_binding_for(choices,scope,QA_ROLE_MOVEMENT,"");
        const qa_launch_binding *entities=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        for (size_t i=0;i<choices->provider_count;++i) {
            const qa_launch_provider *provider=choices->providers+i;
            const qa_product *product=qa_catalog_product(catalog,provider->product);
            if (!product) continue;
            if (movement && !strcmp(provider->instance,movement->instance))
                dialect=product->family==QA_GAME_Q1?QA_CONSOLE_Q1:product->family==QA_GAME_Q2?QA_CONSOLE_Q2:QA_CONSOLE_Q3;
            if (entities && !strcmp(provider->instance,entities->instance)) entities_family=product->family;
        }
    }
    const qa_input_weapon_binding *weapons=NULL; size_t weapon_count=0;
    if (seat->library && !qa_ui_library_weapon_bindings(seat->library,&weapons,&weapon_count,error)) return false;
    qa_input_weapon_binding *live_weapons=NULL;
    qa_actor_id actor;
    if (frontend_seat_actor_read(seat->frontend,seat->id,&actor)) {
        qa_inventory *inventory=qa_application_inventory(seat->frontend->application); size_t definitions=0;
        if (!qa_inventory_item_definitions(inventory,actor,NULL,0,&definitions,error)) return false;
        if (definitions>SIZE_MAX/sizeof(qa_item_definition) || definitions>SIZE_MAX/sizeof(*live_weapons))
            return frontend_fail(error,QA_ERROR_MEMORY,"Registered binding item catalog is too large");
        qa_item_definition *items=definitions?malloc(definitions*sizeof(*items)):NULL;
        live_weapons=definitions?calloc(definitions,sizeof(*live_weapons)):NULL;
        if (definitions && (!items || !live_weapons)) { free(items); free(live_weapons); return frontend_fail(error,QA_ERROR_MEMORY,"Reading registered binding items"); }
        if (!qa_inventory_item_definitions(inventory,actor,items,definitions,&definitions,error)) { free(items); free(live_weapons); return false; }
        qa_strings *strings=qa_session_strings(qa_application_session(seat->frontend->application)); size_t count=0;
        for (size_t i=0;i<definitions;++i) {
            if (!items[i].weapon && !(items[i].actions&QA_ITEM_USE)) continue;
            const char *item_id=qa_strings_cstr(strings,items[i].item);
            if (!item_id) { free(items); free(live_weapons); return frontend_fail(error,QA_ERROR_FORMAT,"Registered binding item has no identity"); }
            qa_input_weapon_binding *target=live_weapons+count++;
            *target=(qa_input_weapon_binding){.id=item_id,.label=items[i].label?items[i].label:item_id,.powerup=!items[i].weapon};
            for (size_t j=0;j<weapon_count;++j) if (!strcmp(weapons[j].id,item_id)) {
                target->q1_impulse=weapons[j].q1_impulse; target->q3_weapon=weapons[j].q3_weapon; break;
            }
        }
        free(items); weapons=live_weapons; weapon_count=count;
    }
    size_t bound_count=qa_input_seat_binding_count(seat->input);
    size_t base=sizeof(shared_actions)/sizeof(*shared_actions)+5;
    if (weapon_count>SIZE_MAX/2 || bound_count>SIZE_MAX-base || weapon_count>SIZE_MAX-base-bound_count) {
        free(live_weapons); return frontend_fail(error,QA_ERROR_MEMORY,"Binding action catalog is too large"); }
    size_t capacity=base+bound_count+weapon_count, text_capacity=bound_count+weapon_count*2;
    binding_action *actions=calloc(capacity,sizeof(*actions));
    char **custom=calloc(text_capacity?text_capacity:1,sizeof(*custom));
    const char **resolved=calloc(bound_count?bound_count:1,sizeof(*resolved));
    if (!actions || !custom || !resolved) { free(actions); free(custom); free(resolved); free(live_weapons); return frontend_fail(error,QA_ERROR_MEMORY,"Reading binding actions"); }
    for (size_t i=0;i<bound_count;++i) resolved[i]=weapon_item(binding_text(qa_input_seat_binding_at(seat->input,i)),weapons,weapon_count);
    size_t action_count = sizeof(shared_actions)/sizeof(*shared_actions), custom_count = 0;
    memcpy(actions, shared_actions, sizeof(shared_actions));
    actions[action_count++] = (binding_action){"Show scores", entities_family == QA_GAME_Q2 ? "score" : "+scores"};
    if (entities_family != QA_GAME_Q1) {
        actions[action_count++] = (binding_action){"Chat", "messagemode"};
        actions[action_count++] = (binding_action){"Team chat", "messagemode2"};
    }
    if (choices) for (size_t i = 0; i < choices->equipment_count; ++i) {
        if (choices->equipment[i].selection.grapple != QA_GRAPPLE_DISABLED &&
            choices->equipment[i].selection.binding == QA_EQUIPMENT_OFFHAND) {
            actions[action_count++] = (binding_action){"Offhand grapple (hold)", "+grapple"}; break;
        }
    }
    if (choices) for (size_t i = 0; i < choices->equipment_count; ++i) {
        if (choices->equipment[i].selection.grenades.enabled) {
            actions[action_count++] = (binding_action){"Cook / throw offhand grenade", "+grenade"}; break;
        }
    }
    bool ok=true;
    for (size_t i=0;i<weapon_count && ok;++i) {
        size_t label_size=strlen(weapons[i].label)+8, command_size=strlen(weapons[i].id)+5;
        char *label=malloc(label_size),*command=malloc(command_size);
        if (!label || !command) { free(label); free(command); ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining selected arsenal binding actions"); break; }
        snprintf(label,label_size,"%s %s",weapons[i].powerup?"Use":"Select",weapons[i].label);
        snprintf(command,command_size,"use %s",weapons[i].id);
        custom[custom_count++]=label; custom[custom_count++]=command;
        actions[action_count++]=(binding_action){label,command};
    }
    for (size_t i = 0; i < bound_count && ok; ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, i);
        const char *command = binding_text(binding); bool found = false;
        for (size_t j = 0; j < action_count; ++j) if (action_matches(command, actions[j].command,resolved[i])) { found = true; break; }
        if (found) continue;
        size_t size = strlen(command) + 10;
        char *label = malloc(size);
        if (!label) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining custom binding label"); break; }
        snprintf(label, size, "%s: %s", binding->kind == QA_BIND_COMMAND ? "Command" : "Action", command);
        custom[custom_count++] = label;
        actions[action_count++] = (binding_action){label, command};
    }
    size_t count = 0, bytes = 0;
    for (size_t i = 0; i < action_count && ok; ++i) {
        bool include;
        ok = action_matches_query(seat, actions[i],resolved,&include,error);
        if (!ok || !include) continue;
        size_t matches_count = 0;
        for (size_t j = 0; j < bound_count; ++j) {
            const qa_input_binding *binding = qa_input_seat_binding_at(seat->input, j);
            if (!action_matches(binding_text(binding), actions[i].command,resolved[j])) continue;
            ++matches_count;
            bytes += strlen(actions[i].label) + strlen(actions[i].command)*2 + strlen(binding_text(binding)) + 256;
        }
        if (!matches_count) bytes += strlen(actions[i].label) + strlen(actions[i].command)*3 + 256;
        count += matches_count ? matches_count : 1;
    }
    size_t catalog_bytes=0;
    for(size_t i=0;i<weapon_count;++i) catalog_bytes+=strlen(weapons[i].id)+strlen(weapons[i].label)+2;
    if(weapon_count>SIZE_MAX/sizeof(*weapons) || catalog_bytes>SIZE_MAX-weapon_count*sizeof(*weapons) ||
        bytes>SIZE_MAX-catalog_bytes-weapon_count*sizeof(*weapons)) ok=frontend_fail(error,QA_ERROR_MEMORY,"Binding item cache is too large");
    if(ok) bytes+=catalog_bytes+weapon_count*sizeof(*weapons);
    size_t reserved = count ? count : 1;
    if (ok && (reserved > SIZE_MAX/sizeof(binding_record) || reserved > SIZE_MAX/sizeof(qa_ui_row) ||
        bytes > SIZE_MAX - reserved*sizeof(binding_record))) ok = frontend_fail(error, QA_ERROR_MEMORY, "Binding table is too large");
    bytes += reserved*sizeof(binding_record);
    if (ok && reserved > seat->binding_capacity) {
        qa_ui_row *rows = realloc(seat->binding_rows, reserved*sizeof(*rows));
        if (!rows) ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining binding table");
        else { seat->binding_rows = rows; seat->binding_capacity = reserved; }
    }
    if (ok && bytes > seat->binding_label_capacity) {
        char *labels = realloc(seat->binding_labels, bytes);
        if (!labels) ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining binding table text");
        else { seat->binding_labels = labels; seat->binding_label_capacity = bytes; }
    }
    if (ok) {
        memset(seat->binding_rows, 0, seat->binding_capacity*sizeof(*seat->binding_rows));
        binding_record *records = (binding_record *)seat->binding_labels;
        qa_input_weapon_binding *catalog_copy=(qa_input_weapon_binding *)(records+reserved);
        char *cursor=(char *)(catalog_copy+weapon_count);
        for(size_t i=0;i<weapon_count;++i) {
            catalog_copy[i]=weapons[i]; catalog_copy[i].id=retain_text(&cursor,weapons[i].id);
            catalog_copy[i].label=retain_text(&cursor,weapons[i].label); catalog_copy[i].default_key=NULL;
        }
        size_t row_count = 0;
        for (size_t i = 0; i < action_count && ok; ++i) {
            bool include;
            ok = action_matches_query(seat, actions[i],resolved,&include,error);
            if (!ok || !include) continue;
            size_t added = 0;
            for (size_t j = 0; j <= bound_count; ++j) {
                const qa_input_binding *binding = j < bound_count ? qa_input_seat_binding_at(seat->input, j) : NULL;
                if (binding && !action_matches(binding_text(binding), actions[i].command,resolved[j])) continue;
                if (!binding && added) break;
                char physical[128] = "Unbound";
                if (binding && !qa_input_physical_name(binding->input, physical, sizeof(physical))) {
                    ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Binding has no physical name"); break;
                }
                const char *canonical = retain_text(&cursor, actions[i].command);
                char suffix[80] = "";
                if (binding) snprintf(suffix, sizeof(suffix), ":%u:%d:%u:%u", (unsigned)binding->input.kind,
                    binding->input.device, binding->input.code, (unsigned)binding->input.positive);
                char *row_key = cursor;
                size_t canonical_size = strlen(canonical), suffix_size = strlen(suffix) + 1;
                memmove(cursor, canonical, canonical_size);
                memcpy(cursor + canonical_size, suffix, suffix_size);
                cursor += canonical_size + suffix_size;
                binding_record *record = records + row_count;
                *record = (binding_record){.cells={retain_text(&cursor, actions[i].label), retain_text(&cursor, physical)},
                    .command=retain_text(&cursor, binding ? binding_text(binding) : canonical), .canonical=canonical,
                    .input=binding ? binding->input : (qa_physical_input){0}, .bound=binding != NULL,.items=catalog_copy,.item_count=weapon_count};
                seat->binding_rows[row_count++] = (qa_ui_row){.key=row_key, .label=record->cells[0], .detail=record->cells[1],
                    .enabled=true, .cells=record->cells, .cell_count=2, .action_label=binding ? "X" : NULL};
                ++added;
            }
        }
        seat->selected_binding = 0;
        for (size_t i = 0; i < count; ++i)
            if (seat->binding_selected && !strcmp(seat->binding_rows[i].key, seat->binding_selected)) seat->selected_binding = i;
        if (ok) ok = set_selected(seat, count ? seat->binding_rows[seat->selected_binding].key : NULL, error);
        if (!count) seat->binding_rows[0] = (qa_ui_row){.key="empty", .label="No matching actions"};
        static const float columns[] = {316,208};
        const binding_record *current = count ? records + seat->selected_binding : NULL;
        const qa_scene_rect_f rects[] = {{48,136,544,240},{48,80,544,28},{48,384,176,28},
            {232,384,176,28},{416,384,176,28},{48,112,316,24},{416,420,176,28},{48,420,100,28},{364,112,228,24}};
        const char *labels[] = {"Bindings","Search actions","Add binding","Remove key","Clear action","Action","Reset bindings","Back","Key"};
        for (size_t i = 0; i < 9; ++i) seat->controls[i] = (qa_ui_control){.id=i+1,.kind=QA_UI_BUTTON,
            .label=labels[i],.rect=rects[i],.enabled=true,.visible=true,.context=seat,.action=action};
        seat->controls[0].kind=QA_UI_LIST; seat->controls[0].enabled=count!=0;
        seat->controls[0].value.list.rows=seat->binding_rows; seat->controls[0].value.list.count=count ? count : 1;
        seat->controls[0].value.list.selected=seat->selected_binding; seat->controls[0].value.list.row_height=24;
        seat->controls[0].value.list.column_widths=columns; seat->controls[0].value.list.column_count=2;
        uint64_t revision = UINT64_C(14695981039346656037);
        for (size_t i = 0; i < count; ++i) for (const unsigned char *p=(const unsigned char *)seat->binding_rows[i].key; *p; ++p)
            revision=(revision^*p)*UINT64_C(1099511628211);
        seat->controls[0].value.list.revision=revision;
        seat->controls[1].kind=QA_UI_FIELD; seat->controls[1].value.field.text=seat->binding_query;
        seat->controls[1].value.field.maximum=80;
        seat->controls[2].enabled=current!=NULL;
        seat->controls[3].enabled=seat->controls[4].enabled=current && current->bound;
        seat->controls[5].enabled=seat->controls[8].enabled=false;
        seat->controls[6].enabled=reset_available(seat);
        *out=(qa_ui_menu){.id=FRONTEND_BINDINGS,.title="Bindings",.controls=seat->controls,.count=9};
    }
    for (size_t i = 0; i < custom_count; ++i) free(custom[i]);
    free(custom); free(actions); free(resolved); free(live_weapons); return ok;
}
bool frontend_bindings_create(frontend_seat *seat, qa_error *error)
{
    return qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_BINDINGS,
        .context = seat, .factory = menu, .close = frontend_binding_cancel}, error) &&
        qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_BINDINGS_CONFLICT,
            .context = seat, .factory = conflict_menu, .close = frontend_binding_cancel}, error) &&
        qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_BINDINGS_RESET,
            .context = seat, .factory = reset_menu}, error);
}
void frontend_bindings_destroy(frontend_seat *seat)
{
    free(seat->binding_rows); free(seat->binding_labels); free(seat->binding_command); free(seat->binding_selected);
    seat->binding_rows = NULL; seat->binding_labels = seat->binding_command = seat->binding_selected = NULL;
}
