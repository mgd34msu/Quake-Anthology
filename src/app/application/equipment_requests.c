#include "equipment_requests.h"
#include "equipment_runtime.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons_services.h"
#include "guest_q3_catalog.h"
#include "guest_q3_client_console.h"
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

static bool q3_item_weapon(application_provider *provider, qa_item_id item,
    int32_t *weapon, qa_error *error)
{
    *weapon = -1;
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3) {
        for (int32_t i = 1; i < QA_Q3_WEAPON_COUNT; ++i)
            if (qa_q3_weapon_item(provider->state.q3, (qa_q3_weapon)i, false) == item) {
                *weapon = i; break;
            }
    } else if (provider && (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine))) {
        struct application_q3_guest *engine = q3g_engine(provider);
        if (engine && engine->game && engine->game->catalog) {
            const application_q3_catalog_weapon *weapons;
            size_t count;
            if (!application_q3_catalog_weapons(engine->game->catalog, &weapons, &count, error))
                return false;
            for (size_t i = 0; i < count; ++i)
                if (weapons[i].item == item) { *weapon = weapons[i].weapon; break; }
        }
    }
    return true;
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

qa_command_result application_equipment_item_resolve(qa_application *app,
    qa_actor_id actor, const qa_command_invocation *command, qa_arena *storage,
    qa_item_definition *item, qa_application_item_selection *selection, qa_error *error)
{
    if (item) *item = (qa_item_definition){0};
    if (selection) *selection = (qa_application_item_selection){.q3_weapon = -1};
    if (command->argc < 2 || !actor.registry ||
        (!qa_text_ascii_equal(command->argv[0], "use") &&
         !qa_text_ascii_equal(command->argv[0], "weapon") &&
         !qa_text_ascii_equal(command->argv[0], "drop"))) return QA_COMMAND_UNHANDLED;
    qa_item_definition *items = NULL;
    size_t count = 0;
    if (!named_items_read(app, actor, storage, &items, &count, error))
        return QA_COMMAND_FAILED;
    bool weapon = qa_text_ascii_equal(command->argv[0], "weapon");
    qa_item_name_match match;
    if (!qa_item_name_resolve(qa_session_strings(app->session), items, count,
        command->argv + 1, command->argc - 1, weapon, storage, &match, error))
        return QA_COMMAND_FAILED;
    if (!match.count) return QA_COMMAND_UNHANDLED;
    if (match.count > 1) {
        size_t extent = strlen(match.alternatives) + 32;
        char *message = qa_arena_alloc(storage, extent, 1, error);
        if (!message) return QA_COMMAND_FAILED;
        snprintf(message, extent, "Ambiguous %s; use %s\n", weapon ? "weapon" : "item",
            match.alternatives);
        qa_console_emit(command->console, &command->context, message);
        return QA_COMMAND_HANDLED;
    }
    qa_item_definition selected = items[match.index];
    qa_application_item_selection resolved = {.item = selected.item,
        .provider = selected.owner, .q3_weapon = -1};
    if (selection && selected.weapon) {
        application_provider *provider = application_provider_lookup(app, selected.owner,
            APPLICATION_PROVIDER_PUBLISHED);
        if (!q3_item_weapon(provider, selected.item, &resolved.q3_weapon, error))
            return QA_COMMAND_FAILED;
    }
    if (item) {
        const char *label = selected.label ? selected.label
            : qa_strings_cstr(qa_session_strings(app->session), selected.item);
        selected.label = qa_arena_text(storage, label, error);
        if (!selected.label) return QA_COMMAND_FAILED;
        *item = selected;
    }
    if (selection) *selection = resolved;
    return QA_COMMAND_HANDLED;
}

static qa_command_result named_item_action(qa_application *app, qa_actor_id actor,
    const qa_command_invocation *command, const qa_item_definition *selected,
    qa_arena *storage, const qa_application_item_selection *requested,
    qa_application_item_selection *selection, qa_error *error)
{
    bool weapon = qa_text_ascii_equal(command->argv[0], "weapon");
    bool use = !qa_text_ascii_equal(command->argv[0], "drop");
    qa_item_action action = use ? QA_ITEM_USE : QA_ITEM_DROP;
    if (!weapon && (selected->actions & action)) {
        return qa_inventory_item_action(app->inventory, actor,
            selected->item, action, error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
    }
    bool accepted = false;
    if (use && selected->weapon) {
        if (!qa_application_equipment_request_weapon(app, actor,
            selected->owner, selected->item, &accepted, error)) return QA_COMMAND_FAILED;
        if (accepted) {
            if (selection) *selection = *requested;
            return QA_COMMAND_HANDLED;
        }
    }
    size_t extent = strlen(selected->label) + 32;
    char *message = qa_arena_alloc(storage, extent, 1, error);
    if (!message) return QA_COMMAND_FAILED;
    snprintf(message, extent, "%s cannot be %s.\n", selected->label, use ? "used" : "dropped");
    qa_console_emit(command->console, &command->context, message);
    return QA_COMMAND_HANDLED;
}

qa_command_result qa_application_item_command(qa_application *app, qa_actor_id actor,
    const qa_command_invocation *command, qa_application_item_selection *selection,
    qa_error *error)
{
    if (selection) *selection = (qa_application_item_selection){.q3_weapon = -1};
    qa_arena storage = {0};
    qa_console_command_arena(command->console, &storage);
    qa_item_definition item;
    qa_application_item_selection requested = {.q3_weapon = -1};
    qa_command_result result = application_equipment_item_resolve(app, actor, command,
        &storage, &item, selection ? &requested : NULL, error);
    if (result == QA_COMMAND_HANDLED && item.item)
        result = named_item_action(app, actor, command, &item, &storage,
            &requested, selection, error);
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

bool qa_application_q1_wheel_item_read(qa_application *app, qa_actor_id actor,
    qa_actor_owner source, int32_t impulse, const uint32_t *authored_weapon_bits,
    qa_item_definition *out, bool *found, qa_error *error)
{
    *found = false;
    qa_id_index_cursor cursor = application_provider_lookup_begin(app, source, APPLICATION_PROVIDER_PUBLISHED);
    application_provider *provider = application_provider_lookup_next(app, &cursor);
    if (!provider || !provider->product || provider->product->family != QA_GAME_Q1) return true;
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_item_id item;
        bool mapped;
        if (!authored_weapon_bits && (impulse < 0 || impulse > UINT8_MAX)) return true;
        if (!qa_q1_player_weapon_impulse_read(provider->state.q1, actor,
            impulse >= 0 && impulse <= UINT8_MAX ? (uint8_t)impulse : 0,
            authored_weapon_bits, &item, &mapped, error)) return false;
        return !mapped || qa_inventory_item_definition_find(app->inventory, actor, &source, item, out, found, error);
    }
    if (provider->kind != APPLICATION_PROVIDER_QC ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    qa_application_qc_player_ui view;
    if (!qa_application_qc_selected_player_ui_read(app, actor, QA_ROLE_ARSENAL, &view, error)) return false;
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < view.binding_count; ++i) {
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        if (profile && !profile->weapon_values[i].ui_declared) continue;
        qa_application_qc_weapon_ui_binding binding;
        uint32_t via;
        if (!application_qc_weapon_binding_at(provider->state.qc.engine, i, &binding, &via, error)) return false;
        if (binding.impulse == impulse) { selected = i; break; }
        if (selected == SIZE_MAX && authored_weapon_bits && binding.bit == *authored_weapon_bits) selected = i;
    }
    if (selected == SIZE_MAX) return true;
    if (!qa_application_qc_message_player_ui_definition(app, &view, selected, out, error)) return false;
    *found = true;
    return true;
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
        if(!engine||!engine->game||!engine->game->catalog)
            return application_fail(e,QA_ERROR_NOT_FOUND,"Original weapon request lost its actual Source catalog");
        int32_t weapon;
        if(!q3_item_weapon(provider,item,&weapon,e)) return false;
        declared=weapon>=0;
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
        if(engine->game->weapon_services)
            ok=application_q3_weapons_services_select_intent(engine->game->weapon_services,actor,owner,item,accepted,e);
        else {
            int32_t weapon;
            if(!q3_item_weapon(provider,item,&weapon,e)) return false;
            ok=application_guest_q3_client_weapon(provider,actor,weapon,accepted,e);
        }
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
