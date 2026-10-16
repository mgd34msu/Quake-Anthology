#include "equipment_requests.h"
#include "equipment_runtime.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons_services.h"
#include "guest_q3_catalog.h"
#include "guest_native_q2_private.h"
#include "guest_qc_items.h"
#include "guest_qc_item_weapons.h"
#include "native_maps.h"
#include "qa/application_qc_presentation.h"
#include "qa/application_equipment.h"
#include "qa/text.h"

#include <stdio.h>
#include <string.h>

static void named_item_append(qa_item_definition *items, size_t *count,
    qa_item_definition item)
{
    for (size_t i = 0; i < *count; ++i)
        if (items[i].item == item.item) return;
    items[(*count)++] = item;
}

static bool named_items_read(qa_application *app, qa_actor_id actor, qa_arena *storage,
    qa_item_definition **out, size_t *count, qa_error *error)
{
    size_t definitions, weapons = 0;
    if (!qa_inventory_item_definitions(app->inventory, actor, NULL, 0, &definitions, error))
        return false;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    const application_q3_catalog_weapon *q3 = NULL;
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_QC) {
        weapons = arsenal->state.qc.qualified ? arsenal->state.qc.qualified->weapon_count : 11;
    } else if (arsenal && (arsenal->kind == APPLICATION_PROVIDER_QVM ||
        (arsenal->kind == APPLICATION_PROVIDER_NATIVE && arsenal->state.native.engine))) {
        struct application_q3_guest *engine = q3g_engine(arsenal);
        if (!application_q3_catalog_weapons(engine->game->catalog, &q3, &weapons, error))
            return false;
    }
    qa_equipment_weapon_view gear;
    bool has_gear = false;
    if (app->equipment && !qa_equipment_weapon_view_read(app->equipment, actor,
        &gear, &has_gear, error)) return false;
    if (weapons == SIZE_MAX || definitions > SIZE_MAX - weapons - 1 ||
        definitions + weapons + 1 > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_MEMORY, "Item command catalog exceeds its storage extent");
    size_t capacity = definitions + weapons + (has_gear ? 1 : 0);
    *out = capacity ? qa_arena_alloc(storage, capacity * sizeof(**out),
        _Alignof(qa_item_definition), error) : NULL;
    if (capacity && !*out) return false;
    if (!qa_inventory_item_definitions(app->inventory, actor, *out, definitions, count, error))
        return false;
    for (size_t i = 0; i < weapons; ++i) {
        qa_item_definition item;
        if (q3) item = (qa_item_definition){.item = q3[i].item, .ammo = q3[i].ammo,
            .owner = arsenal->owner, .label = q3[i].label, .weapon = true};
        else {
            const struct application_qc_profile *profile = arsenal->state.qc.qualified;
            if (profile && !profile->weapon_values[i].ui_declared) continue;
            qa_application_qc_weapon_ui_binding binding;
            uint32_t via;
            qa_error lookup = {0};
            if (!application_qc_weapon_binding_at(arsenal->state.qc.engine, i, &binding, &via, &lookup)) {
                if (lookup.code == QA_OK) break;
                if (error) *error = lookup;
                return false;
            }
            item = (qa_item_definition){.item = binding.item, .owner = arsenal->owner,
                .label = binding.label, .weapon = true};
        }
        named_item_append(*out, count, item);
    }
    if (has_gear) named_item_append(*out, count, (qa_item_definition){.item = gear.item,
        .owner = gear.source.owner, .label = gear.label, .weapon = true});
    return true;
}

static bool item_label_matches(const char *label, const char *requested)
{
    for (;;) {
        while (*label == ' ') ++label;
        if (*label != *requested) return false;
        if (!*label) return true;
        ++label; ++requested;
    }
}

static bool named_item_text(qa_application *app, const qa_command_invocation *command,
    qa_item_definition *items, size_t count, qa_arena *storage, qa_buffer *folded,
    size_t *extent, qa_error *error)
{
    size_t size = 1;
    for (size_t i = 1; i < command->argc; ++i) {
        size_t length = strlen(command->argv[i]);
        if (length > SIZE_MAX - size) goto overflow;
        size += length;
    }
    qa_strings *strings = qa_session_strings(app->session);
    for (size_t i = 0; i < count; ++i) {
        size_t identity = strlen(qa_strings_cstr(strings, items[i].item)) + 1;
        size_t label = strlen(items[i].label) + 1;
        if (identity > SIZE_MAX - size || label > SIZE_MAX - size - identity) goto overflow;
        size += identity + label;
    }
    if (size > SIZE_MAX - 64) goto overflow;
    *extent = size + 64;
    char *text = qa_arena_alloc(storage, size, 1, error);
    if (!text) return false;
    char *at = text;
    for (size_t i = 1; i < command->argc; ++i)
        for (const char *s = command->argv[i]; *s; ++s)
            if (*s != ' ') *at++ = *s;
    *at++ = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *identity = qa_strings_cstr(strings, items[i].item);
        size_t length = strlen(identity) + 1;
        memcpy(at, identity, length); at += length;
        length = strlen(items[i].label) + 1;
        memcpy(at, items[i].label, length);
        items[i].label = at; at += length;
    }
    /* One conversion folds the request and every candidate, including UTF-8
     * mod labels, without allocating a temporary string for each row. */
    return qa_utf8_lower_arena((qa_bytes){(const uint8_t *)text, (size_t)(at - text)},
        storage, folded, error);
overflow:
    return application_fail(error, QA_ERROR_MEMORY, "Item command names exceed their storage extent");
}

static qa_command_result named_item_dispatch(qa_application *app,
    const qa_command_invocation *command, qa_item_definition *items, size_t count,
    qa_arena *storage, qa_error *error)
{
    qa_buffer folded;
    size_t extent = 0;
    if (!named_item_text(app, command, items, count, storage, &folded, &extent, error))
        return QA_COMMAND_FAILED;
    const char *requested = (const char *)folded.data;
    bool weapon = qa_text_ascii_equal(command->argv[0], "weapon");
    qa_item_definition *selected = NULL;
    size_t matches = 0;
    const char *at = requested + strlen(requested) + 1;
    for (size_t i = 0; i < count; ++i) {
        const char *identity = at;
        const char *label = identity + strlen(identity) + 1;
        at = label + strlen(label) + 1;
        if (weapon && !items[i].weapon) continue;
        if (!strcmp(identity, requested)) { selected = items + i; matches = 1; break; }
    }
    if (!selected) {
        at = requested + strlen(requested) + 1;
        for (size_t i = 0; i < count; ++i) {
            const char *label = at + strlen(at) + 1;
            at = label + strlen(label) + 1;
            if ((!weapon || items[i].weapon) && item_label_matches(label, requested)) {
                selected = items + i; ++matches;
            }
        }
    }
    if (!selected) return QA_COMMAND_UNHANDLED;
    char *message = qa_arena_alloc(storage, extent, 1, error);
    if (!message) return QA_COMMAND_FAILED;
    if (matches > 1) {
        size_t used = (size_t)snprintf(message, extent, "Ambiguous %s; use ",
            weapon ? "weapon" : "item");
        size_t included = 0;
        at = requested + strlen(requested) + 1;
        for (size_t i = 0; i < count; ++i) {
            const char *label = at + strlen(at) + 1;
            at = label + strlen(label) + 1;
            if ((!weapon || items[i].weapon) && item_label_matches(label, requested))
                used += (size_t)snprintf(message + used, extent - used, "%s%s",
                    included++ ? ", " : "", qa_strings_cstr(qa_session_strings(app->session), items[i].item));
        }
        memcpy(message + used, "\n", 2);
        qa_console_emit(command->console, &command->context, message);
        return QA_COMMAND_HANDLED;
    }
    bool use = !qa_text_ascii_equal(command->argv[0], "drop");
    qa_item_action action = use ? QA_ITEM_USE : QA_ITEM_DROP;
    if (!weapon && (selected->actions & action)) {
        return qa_inventory_item_action(app->inventory, command->context.actor,
            selected->item, action, error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
    }
    bool accepted = false;
    if (use && selected->weapon) {
        if (!qa_application_equipment_request_weapon(app, command->context.actor,
            selected->owner, selected->item, &accepted, error)) return QA_COMMAND_FAILED;
        if (accepted) return QA_COMMAND_HANDLED;
    }
    snprintf(message, extent, "%s cannot be %s.\n", selected->label, use ? "used" : "dropped");
    qa_console_emit(command->console, &command->context, message);
    return QA_COMMAND_HANDLED;
}

qa_command_result application_equipment_named_command(qa_application *app,
    const qa_command_invocation *command, qa_error *error)
{
    if (command->argc < 2 || !command->context.actor.registry ||
        (!qa_text_ascii_equal(command->argv[0], "use") &&
         !qa_text_ascii_equal(command->argv[0], "weapon") &&
         !qa_text_ascii_equal(command->argv[0], "drop"))) return QA_COMMAND_UNHANDLED;
    qa_arena storage = {0};
    qa_console_command_arena(command->console, &storage);
    qa_item_definition *items = NULL;
    size_t count = 0;
    qa_command_result result = named_items_read(app, command->context.actor, &storage,
        &items, &count, error) ? named_item_dispatch(app, command, items, count, &storage, error)
                             : QA_COMMAND_FAILED;
    qa_arena_destroy(&storage);
    return result;
}

uint8_t qa_application_offhand_actions(const qa_application *app,qa_actor_id actor)
{
    qa_equipment_state state;
    if (!app || !app->equipment || !qa_equipment_read(app->equipment,actor,&state)) return 0;
    return (state.selection.binding==QA_EQUIPMENT_OFFHAND && state.selection.grapple!=QA_GRAPPLE_DISABLED
        ? QA_USERCMD_GRAPPLE : 0) | (state.selection.grenades.enabled ? QA_USERCMD_GRENADE : 0);
}

static bool current(qa_application *app,application_provider *provider,qa_actor_id actor)
{
    return app&&provider&&provider->constructed&&provider->attached&&!provider->close_pending&&
        qa_actors_get(qa_session_actors(app->session),actor)&&
        application_provider_for(app,actor,QA_ROLE_ARSENAL,"")==provider;
}
typedef struct equipment_q2_request {
    qa_q2_game *game;
    qa_q2_weapon weapon;
    qa_q2_selection result;
} equipment_q2_request;
static bool q2_request(void *context,qa_actor_id actor,qa_error *e)
{
    equipment_q2_request *request=context;
    return qa_q2_weapon_use(request->game,actor,request->weapon,&request->result,e);
}
bool application_equipment_primary_accepts(void *context,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    qa_application *app=context;
    application_provider *provider=app?application_provider_for(app,actor,QA_ROLE_ARSENAL,""):NULL;
    if(!accepted||!owner||!item||!current(app,provider,actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Weapon request requires its genuine selected arsenal and full actor");
    *accepted=false;
    if(provider->owner!=owner) return true;
    if(provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine)
        return application_native_q2_weapon_accepts(provider,actor,item,accepted,e);
    bool declared=false;
    if(provider->kind==APPLICATION_PROVIDER_QVM ||
        (provider->kind==APPLICATION_PROVIDER_NATIVE && provider->state.native.engine)) {
        struct application_q3_guest *engine=q3g_engine(provider);
        const application_q3_catalog_weapon *weapons=NULL; size_t n=0;
        if(!engine||!engine->game||!engine->game->catalog)
            return application_fail(e,QA_ERROR_NOT_FOUND,"Original weapon request lost its actual Source catalog");
        if(!application_q3_catalog_weapons(engine->game->catalog,&weapons,&n,e)) return false;
        for(size_t i=0;i<n;++i) if(weapons[i].item==item) declared=true;
    } else if(provider->kind==APPLICATION_PROVIDER_QC) {
        if(provider->state.qc.qualified && provider->state.qc.qualified->items &&
            provider->state.qc.qualified->items->weapons)
            return application_qc_item_weapons_accepts(provider->state.qc.engine,actor,item,accepted,e);
        qa_application_qc_player_ui view;
        if(!qa_application_qc_selected_player_ui_read(app,actor,QA_ROLE_ARSENAL,&view,e)) return false;
        for(size_t i=0;i<view.binding_count;++i) {
            qa_application_qc_weapon_ui_binding binding;
            if(!qa_application_qc_message_player_ui_binding(app,&view,i,&binding,e)) return false;
            if(binding.item==item) declared=true;
        }
        if(!qa_application_qc_message_player_ui_current(app,&view))
            return application_fail(e,QA_ERROR_NOT_FOUND,"QC weapon request replaced its true declaration owner");
    } else {
        qa_item_definition definition;
        bool native=provider->kind==APPLICATION_PROVIDER_Q1||provider->kind==APPLICATION_PROVIDER_Q2||
            provider->kind==APPLICATION_PROVIDER_Q3;
        if(!qa_inventory_item_definition_find(app->inventory,actor,native?&owner:NULL,item,&definition,&declared,e)) return false;
        declared=declared&&definition.owner==owner&&definition.weapon;
    }
    if(!declared) return true;
    double quantity=0;
    if(!qa_inventory_count_read(app->inventory,actor,item,&quantity,e)) return false;
    if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request source changed during its quantity read");
    if(quantity>0) *accepted=true;
    else if(provider->kind==APPLICATION_PROVIDER_NATIVE||provider->kind==APPLICATION_PROVIDER_QVM) {
        qa_item_id active=0;
        if(!qa_application_weapon_read(app,actor,&active,e)) return false;
        if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request source changed during its active read");
        *accepted=active==item;
    }
    return true;
}
bool application_equipment_primary_select(void *context,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    qa_application *app=context;
    if(!application_equipment_primary_accepts(app,actor,owner,item,accepted,e)) return false;
    if(!*accepted) return true;
    application_provider *provider=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    bool ok;
    if(provider->kind==APPLICATION_PROVIDER_QC)
        ok=qa_application_qc_weapon_request(app,actor,item,accepted,e);
    else if(provider->kind==APPLICATION_PROVIDER_QVM||
        (provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.engine)) {
        struct application_q3_guest *engine=q3g_engine(provider);
        if(!engine||!engine->game||!engine->game->weapon_services)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Original weapon request has no admitted Source request owner");
        ok=application_q3_weapons_services_select_intent(engine->game->weapon_services,actor,owner,item,accepted,e);
    } else if(provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine)
        ok=application_native_q2_weapon_request(provider,actor,item,accepted,e);
    else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        const char *identity=qa_strings_cstr(qa_session_strings(app->session),item);
        equipment_q2_request request={.game=provider->state.q2,.result=QA_Q2_NOT_OWNED};
        bool found=false;
        for(int i=1;i<QA_Q2_WEAPON_COUNT;++i) {
            const qa_q2_weapon_definition *definition=qa_q2_weapon_definition_at(provider->state.q2,(qa_q2_weapon)i);
            if(identity&&definition&&definition->item&&!strcmp(identity,definition->item)) {
                request.weapon=(qa_q2_weapon)i; found=true; break;
            }
        }
        if(!found) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request lost its actual selected Q2 definition");
        ok=qa_q2_run_actor(request.game,actor,q2_request,&request,e);
        if(ok) *accepted=request.result==QA_Q2_SELECTED||request.result==QA_Q2_CURRENT;
    }
    else if(provider->kind==APPLICATION_PROVIDER_Q1||provider->kind==APPLICATION_PROVIDER_Q3)
        ok=application_native_mode_select_weapon(app,actor,item,e);
    else return application_fail(e,QA_ERROR_UNSUPPORTED,"Native external weapon request requires its genuine entered Source command adapter");
    if(!ok) return false;
    if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon selection retired its actual source or full actor");
    return true;
}
bool qa_application_equipment_request_weapon(qa_application *app,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    if(!app||!accepted||!owner||!item||!app->session||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Weapon request requires a genuine current application actor and source");
    *accepted=false;
    qa_equipment_state bound;
    if(app->equipment&&qa_equipment_read(app->equipment,actor,&bound))
        return qa_equipment_weapon_request(app->equipment,actor,owner,item,accepted,e);
    return application_equipment_primary_select(app,actor,owner,item,accepted,e);
}
