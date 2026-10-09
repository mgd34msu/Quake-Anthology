#include "internal.h"
#include "guest_q3_private.h"
#include "guest_qc_internal.h"
#include "guest_qc_items.h"
#include "guest_qc_item_weapons.h"
#include "guest_native_q2_equipment.h"
#include "guest_native_q2_private.h"
#include "guest_q3_fire.h"
#include "guest_q3_catalog.h"
#include "guest_q3_weapon_models.h"
#include "native_q3_equipment.h"
#include "equipment_gear_presentation.h"
#include "guest_q3_components.h"
#include "qa/application_equipment.h"
#include "qa/native_host_q2_wire.h"
#include "qa/qc_weapon_visual.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/game_q1_bots.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

bool qa_application_equipment_q3_product_read(const qa_application *app,
    qa_actor_owner owner, qa_q3_product *out, qa_error *error)
{
    if (!app || !out || !owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment registry requires its actual Q3 product owner");
    application_provider *found = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner) {
            if (found) return application_fail(error, QA_ERROR_FORMAT, "Equipment product has ambiguous actual providers");
            found = app->providers[i];
        }
    if (!found || !found->constructed || found->close_pending || !found->product ||
        found->product->family != QA_GAME_Q3)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment registry lost its actual constructed Q3 provider");
    if (found->kind == APPLICATION_PROVIDER_Q3) {
        *out = !strcmp(found->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
        return true;
    }
    const struct application_q3_guest *engine = q3g_engine(found);
    if (!engine || !engine->game)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment registry has no actual original Q3 GAME owner");
    *out = engine->product; return true;
}

static application_provider *qc_item_provider(qa_application *app,qa_actor_owner owner)
{
    for(size_t i=0;i<app->provider_count;++i){application_provider *provider=app->providers[i];
        if(provider&&provider->owner==owner&&provider->kind==APPLICATION_PROVIDER_QC&&
            provider->constructed&&provider->attached&&!provider->close_pending&&provider->product&&
            provider->launch&&provider->state.qc.engine&&provider->state.qc.qualified&&
            provider->state.qc.qualified->items&&provider->state.qc.qualified->items->weapons)return provider;
    }
    return NULL;
}
bool qa_application_equipment_current(qa_application *app,
    const qa_application_equipment_view *view)
{
    if (!app || !view || !app->session ||
        !qa_actors_get(qa_session_actors(app->session), view->actor)) return false;
    if(view->source_slot) {
        application_q3_component_publication component;
        application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
        qa_equipment_state reached;
        if(!app->equipment||!qa_equipment_read(app->equipment,view->actor,&reached))return false;
        qa_item_id pending=reached.weapon_slot.phase==QA_WEAPON_SLOT_ACTIVE?view->source_binding.pending:reached.weapon_slot.next_item;
        qa_actor_owner pending_owner=pending?(reached.weapon_slot.phase==QA_WEAPON_SLOT_ACTIVE?view->provider:reached.weapon_slot.next_provider):0;
        if(!view->selected||view->equipment_slot||view->original_qvm||
            view->pending!=pending||view->pending_provider!=pending_owner||
            view->gear_namespace||view->gear_service_owner||
            !qa_equipment_weapon_presented(app->equipment,view->actor,view->provider)||
            !qa_equipment_weapon_presentation_current(app->equipment,&view->source_binding)||
            view->source_binding.provider!=view->provider||view->source_binding.active!=view->item||
            !primary||!primary->constructed||!primary->attached||primary->close_pending||primary->owner!=view->primary)return false;
        application_provider *qc=qc_item_provider(app,view->provider);
        if(qc)return qc->product->family==view->family&&view->source_generation==0&&
            view->view_content==qc->launch->content&&
            application_qc_items_actor_current(qc->state.qc.engine,view->actor,NULL);
        return application_q3_components_event_source_read(app,view->provider,&component,NULL)&&
            component.game&&component.product&&component.product->family==view->family&&
            component.generation==view->source_generation&&component.content==view->view_content&&
            application_q3_component_items(component.game)==view->source_binding.context;
    }
    qa_equipment_state slot;
    if(app->equipment&&qa_equipment_read(app->equipment,view->actor,&slot)&&slot.weapon_slot_present&&
        !qa_equipment_weapon_presented(app->equipment,view->actor,view->provider))return false;
    if (!view->equipment_slot && app->equipment &&
        qa_equipment_read(app->equipment, view->actor, &slot) && slot.slot_active &&
        slot.selection.grapple == QA_GRAPPLE_Q3 && slot.selection.binding == QA_EQUIPMENT_WEAPON_SLOT) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_read(app->equipment_runtime, slot.sources.grapple,
            &source, NULL) || source.gear) return false;
    }
    if (view->equipment_slot) {
        qa_equipment_state state;
        application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
        if (!app->equipment || !qa_equipment_read(app->equipment, view->actor, &state) ||
            state.selection.grapple != QA_GRAPPLE_Q3 || state.selection.binding != QA_EQUIPMENT_WEAPON_SLOT ||
            !(state.weapon_slot_present?qa_equipment_weapon_presented(app->equipment,view->actor,view->provider):state.slot_active) || state.sources.grapple != view->provider || !view->selected ||
            view->family != QA_GAME_Q3 || !view->gear_namespace || !view->gear_service_owner ||
            !primary || !primary->constructed || !primary->attached || primary->close_pending ||
            primary->owner != view->primary) return false;
        application_equipment_runtime_source source;
        return application_equipment_runtime_source_read(app->equipment_runtime, view->provider, &source, NULL) &&
            source.gear && source.definition && source.gear_owner == view->gear_namespace &&
            source.service_owner == view->gear_service_owner && source.weapon_item == view->item;
    }
    if (view->gear_namespace || view->gear_service_owner) return false;
    application_provider *provider = application_provider_for(app, view->actor, QA_ROLE_ARSENAL, "");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    return provider && provider->constructed && provider->attached && !provider->close_pending &&
        provider->owner == view->provider && provider->product && provider->product->family == view->family &&
        primary && primary->constructed && primary->attached && !primary->close_pending &&
        primary->owner == view->primary && view->selected == (provider != primary);
}

typedef struct original_q3_model_parents {
    application_provider *provider, *receiver;
    struct application_q3_guest *engine, *receiver_engine;
    q3g_role *cgame;
    qa_q3_presentation_assets *assets;
    uint32_t client;
} original_q3_model_parents;

static bool q3_model_parents(qa_application *app,
    const qa_application_equipment_view *view, qa_actor_owner recipient, uint32_t seat,
    original_q3_model_parents *out, qa_error *error)
{
    if (!app || !view || !out || !recipient || !view->original_qvm || view->equipment_slot ||
        view->family != QA_GAME_Q3 || !view->has_q3_source ||
        view->source_weapon != view->q3_source.weapon || !qa_application_equipment_current(app, view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon models require their actual selected full actor");
    application_provider *provider = application_provider_for(app, view->actor, QA_ROLE_ARSENAL, "");
    struct application_q3_guest *engine = q3g_engine(provider);
    uint32_t client;
    if (!engine || !engine->game || !engine->game->vm || engine->game->retired ||
        !application_q3_guest_actor_client(provider, view->actor, &client))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon models lost their actual GAME client");
    qa_q3_player current_source;
    if (!engine->game->host || !qa_q3_host_source_player(engine->game->host, client, &current_source, error)) return false;
    if (current_source.weapon != view->source_weapon)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon model observation changed its actual source selection");
    q3g_role *found = NULL;
    application_provider *receiver = NULL;
    for (size_t i = 0; i < app->provider_count; ++i) {
        if (app->providers[i]->owner != recipient) continue;
        if (receiver) return application_fail(error, QA_ERROR_FORMAT, "Original weapon models repeat their actual recipient provider");
        receiver = app->providers[i];
    }
    struct application_q3_guest *receiver_engine = q3g_engine(receiver);
    if (!receiver || !receiver->constructed || !receiver->attached || receiver->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon models lost their actual recipient provider");
    *out = (original_q3_model_parents){.provider = provider, .receiver = receiver,
        .engine = engine, .receiver_engine = receiver_engine, .client = client};
    if (!receiver_engine) return true;
    for (q3g_role *role = receiver_engine->roles; role; role = role->next) {
        struct application_q3_guest *source = role->client_engine ? role->client_engine : receiver_engine;
        if (role->kind != QA_QVM_CGAME || role->seat != seat || role->retired || !role->weapon_models ||
            source != engine || source->game != engine->game) continue;
        if (found) return application_fail(error, QA_ERROR_FORMAT, "Original weapon models repeat the actual recipient CG seat");
        found = role;
    }
    if (!found) return true;
    if (!found->vm || !found->host || !found->initialized || !found->init_succeeded)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon registration owner has no completed CG source");
    qa_q3_presentation_assets *assets = qa_q3_host_presentation_resources(found->host);
    if (!assets)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon registration owner has no actual CG model registry");
    if (!application_q3_weapon_models_namespace(found->weapon_models,
        found->vm, engine->game->vm, assets, error)) return false;
    out->cgame = found; out->assets = assets;
    return true;
}

static bool q3_model_parents_current(qa_application *app,
    const qa_application_equipment_view *view, const original_q3_model_parents *parents,
    qa_error *error)
{
    q3g_role *found = parents->cgame;
    application_provider *provider = parents->provider, *receiver = parents->receiver;
    struct application_q3_guest *engine = parents->engine, *receiver_engine = parents->receiver_engine;
    qa_q3_presentation_assets *assets = parents->assets;
    uint32_t current_client;
    if (!qa_application_equipment_current(app, view) || q3g_engine(provider) != engine ||
        !application_q3_guest_actor_client(provider, view->actor, &current_client) || current_client != parents->client ||
        q3g_engine(receiver) != receiver_engine || !receiver->constructed || !receiver->attached || receiver->close_pending ||
        found->retired || found->vm == NULL || found->host == NULL ||
        qa_q3_host_presentation_resources(found->host) != assets)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon model read changed its actual actor or CG registry");
    if (!application_q3_weapon_models_namespace(found->weapon_models,
        found->vm, engine->game->vm, assets, error)) return false;
    return true;
}

bool qa_application_equipment_q3_models_read(qa_application *app,
    const qa_application_equipment_view *view, qa_actor_owner recipient, uint32_t seat,
    qa_application_q3_weapon_models *out, bool *present, qa_error *error)
{
    if (!out || !present)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon model read requires its output");
    *present = false;
    original_q3_model_parents parents;
    if (!q3_model_parents(app, view, recipient, seat, &parents, error)) return false;
    if (!parents.cgame) return true;
    qa_application_q3_weapon_models result; bool available;
    if (!application_q3_weapon_models_read(parents.cgame->weapon_models, view->source_weapon,
            &result, &available, error) || !q3_model_parents_current(app, view, &parents, error)) return false;
    if (available) *out = result;
    *present = available; return true;
}

bool qa_application_equipment_q3_source_draw_match(qa_application *app,
    const qa_application_equipment_view *view, qa_actor_owner recipient, uint32_t seat,
    const qa_q3_presentation_assets *assets, bool *matching, qa_error *error)
{
    if (!assets || !matching)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Source Draw requires its actual recipient registry");
    *matching = false;
    original_q3_model_parents parents;
    if (!q3_model_parents(app, view, recipient, seat, &parents, error)) return false;
    if (!parents.cgame) return true;
    if (parents.assets != assets)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Source Draw displaced its actual CG registry");
    if (!q3_model_parents_current(app, view, &parents, error)) return false;
    *matching = true; return true;
}

static qa_item_id identity(qa_application *app, const char *name)
{
    return name ? qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
}

static bool primary_visibility(qa_application *app, qa_actor_id actor,
    bool *visible, qa_error *error)
{
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear visibility lost its actual primary source");
    if (source->kind == APPLICATION_PROVIDER_QVM ||
        (source->kind == APPLICATION_PROVIDER_NATIVE && source->product->family == QA_GAME_Q3)) {
        struct application_q3_guest *engine = q3g_engine(source);
        uint32_t slot; qa_q3_player player;
        if (!engine || !engine->game || !application_q3_guest_actor_client(source, actor, &slot))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Gear visibility has no actual primary Q3 client");
        if (!qa_q3_host_source_player(engine->game->host, slot, &player, error)) return false;
        *visible = player.stats[0] > 0;
        return true;
    }
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        struct application_native_q2 *engine = source->state.native.q2_engine;
        uint32_t slot = 0;
        for (uint32_t i = 1; i < 257; ++i) {
            const application_native_q2_client *client = engine->clients + i;
            if (!client->connected || !client->begun || client->disconnect_started ||
                !qa_actor_id_equal(client->actor, actor)) continue;
            if (slot) return application_fail(error, QA_ERROR_FORMAT, "Gear visibility repeats a primary Q2 client");
            slot = i;
        }
        bool classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
        if (!slot || !application_native_q2_idle(source) || !engine->initialized || !engine->map_ready ||
            engine->shutting_down || (!classic && engine->profile != QA_NATIVE_Q2_GAME_API2023))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Gear visibility lost its actual primary Q2 client");
        qa_native_slot_binding binding;
        if (!qa_native_slot(qa_native_host_instance(source->state.native.host), slot, &binding, error)) return false;
        if (binding.kind == QA_NATIVE_SLOT_FREE || binding.owner != source->owner ||
            binding.source_slot != slot || !qa_actor_id_equal(binding.actor, actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Gear visibility changed its primary Q2 binding");
        qa_q2_player player;
        bool ok = qa_native_host_q2_player(source->state.native.host, slot, &player, error);
        if (ok) *visible = player.stats[1] > 0;
        return ok;
    }
    qa_actor_id health_actor = actor;
    if (source->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info observer, target;
        if (qa_q2_player_read(source->state.q2, actor, &observer) && observer.spectator &&
            observer.chase_target.registry && qa_q2_player_read(source->state.q2, observer.chase_target, &target) &&
            target.connected && !target.spectator) health_actor = observer.chase_target;
    }
    qa_combat_state combat; qa_application_control_view control;
    if (!qa_combat_read_traits(app->combat, health_actor, &combat, error)) return false;
    if (!qa_application_control_read(app, actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear visibility has no actual primary player traits");
    bool intermission = false;
    if (!application_source_intermission_read(source, &intermission, error)) return false;
    *visible = combat.health > 0 && !control.cutscene && !intermission;
    return true;
}

static qa_item_id q3_identity(qa_application *app, qa_q3_weapon weapon, bool ammo)
{
    const char *name = qa_q3_weapon_identity_name(weapon);
    if (!name || (ammo && (weapon == QA_Q3_W_GAUNTLET || weapon == QA_Q3_W_GRAPPLE))) return 0;
    char key[64];
    snprintf(key, sizeof(key), "q3:%s/%s", ammo ? "ammo" : "weapon", name);
    return identity(app, key);
}

static bool item_definition(qa_application *app, qa_application_equipment_view *view,
    qa_error *error)
{
    if (!view->item) return true;
    qa_item_definition selected; bool found;
    if (!qa_inventory_item_definition_find(app->inventory, view->actor, NULL,
        view->item, &selected, &found, error)) return false;
    if (found && !selected.weapon)
        return application_fail(error, QA_ERROR_FORMAT, "Selected equipment canonical definition is not a weapon");
    if (found) {
        view->label = selected.label; view->ammo = selected.ammo;
        view->has_weapon_status = view->has_start_requirement;
    }
    return true;
}

static bool qc_model(application_provider *provider, qa_actor_id actor,
    qa_application_equipment_view *out, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC equipment requires its actual idle source owner");
    if(provider->state.qc.qualified && provider->state.qc.qualified->items &&
        provider->state.qc.qualified->items->weapons) {
        const application_qc_resource *resource;float source_frame;
        if(!application_qc_item_weapons_model_read(engine,actor,&resource,&source_frame,error))return false;
        double frame=trunc((double)source_frame);
        if(!isfinite(frame) || frame<INT32_MIN || frame>INT32_MAX)
            return application_fail(error,QA_ERROR_FORMAT,"Declared QC weapon frame exceeds its actual presentation extent");
        out->has_frame=true;out->frame=(int32_t)frame;
        if(resource) { out->view_model=resource->name;out->view_source=resource->source;out->visible=true; }
        if(out->item) {
            qa_item_admission admission;bool found;
            if(!application_qc_items_item_read(provider,actor,out->item,&admission,
                &out->source_icon,&out->source_held,&found,error))return false;
            if(!found)return application_fail(error,QA_ERROR_FORMAT,"Declared QC weapon lost its actual item metadata");
        }
        return true;
    }
    qa_qc_weapon_visual source = {0};
    bool found = false;
    for (uint32_t slot = 1; slot < qa_qc_entity_count(provider->state.qc.instance); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QC equipment physical binding is absent");
        if (binding.kind == QA_QC_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor)) continue;
        if (!qa_qc_weapon_visual_read(provider->state.qc.instance, slot, actor, &source, error)) return false;
        found = true; break;
    }
    if (!found) return application_fail(error, QA_ERROR_NOT_FOUND, "QC equipment has no physical source actor");
    out->has_frame = source.has_frame;
    if (source.has_frame) {
        double frame = trunc((double)source.frame);
        if (frame < INT32_MIN || frame > INT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "QC equipment frame exceeds its source integer extent");
        out->frame = (int32_t)frame;
    }
    if (source.has_punch_angle) out->kick_angles = source.punch_angle;
    if (!source.has_model || !source.model[0]) return true;
    const application_qc_resource *resource = NULL;
    for (size_t i = 0; i < engine->resource_count; ++i)
        if (engine->resources[i].kind == QA_QC_RESOURCE_MODEL &&
            !strcmp(engine->resources[i].name, source.model)) {
            resource = engine->resources + i; break;
        }
    if (!resource || !resource->source)
        return application_fail(error, QA_ERROR_NOT_FOUND, "QC weaponmodel has no actual retained MODEL precache");
    out->view_model = resource->name; out->view_source = resource->source; out->visible = true;
    return true;
}

static bool q3_warning(qa_application *app, qa_application_equipment_view *view,
    qa_q3_product product, qa_error *error)
{
    uint32_t total = 0;
    for (qa_q3_weapon weapon = QA_Q3_W_MACHINEGUN; weapon < QA_Q3_WEAPON_COUNT; ++weapon) {
        if (product == QA_Q3_ARENA && weapon > QA_Q3_W_GRAPPLE) continue;
        qa_item_id item = q3_identity(app, weapon, false);
        double owned = 0;
        if (item && !qa_inventory_count_read(app->inventory, view->actor, item, &owned, error)) return false;
        if (!qa_application_equipment_current(app, view))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 warning lost its actual selected arsenal");
        if (owned <= 0) continue;
        qa_item_id ammo = q3_identity(app, weapon, true);
        double count = -1;
        if (ammo && !qa_inventory_count_read(app->inventory, view->actor, ammo, &count, error)) return false;
        if (!qa_application_equipment_current(app, view))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 warning lost its actual selected arsenal");
        if (!isfinite(count) || count < INT32_MIN || count > INT32_MAX || trunc(count) != count)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 warning requires actual source int32 ammo counters");
        bool slow = weapon == QA_Q3_W_ROCKET || weapon == QA_Q3_W_GRENADE ||
            weapon == QA_Q3_W_RAIL || weapon == QA_Q3_W_SHOTGUN || weapon == QA_Q3_W_PROX;
        total += (uint32_t)(int32_t)count * (slow ? 1000u : 200u);
        if (total <= INT32_MAX && total >= 5000) { view->warning = QA_APPLICATION_AMMO_NONE; return true; }
    }
    view->warning = total == 0 ? QA_APPLICATION_AMMO_EMPTY : QA_APPLICATION_AMMO_LOW;
    return true;
}

static bool q1_visibility(qa_application *app, qa_application_equipment_view *view,
    qa_error *error)
{
    if (view->family != QA_GAME_Q1 || !view->visible) return true;
    qa_combat_state combat;
    if (!qa_combat_read_traits(app->combat, view->actor, &combat, error)) return false;
    qa_builtin_services services = application_builtin_services(app, app->world, app->physics);
    qa_builtin_actor_traits traits = {0};
    bool invisible = services.actor_traits && services.actor_traits(services.context, view->actor, &traits) && traits.invisible;
    view->visible = combat.health > 0 && !invisible;
    return true;
}

bool qa_application_equipment_source_read(qa_application *app,qa_actor_id actor,
    qa_application_equipment_view *out,bool *present,qa_error *error)
{
    if (!app || !out || !present || !app->session || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment observation requires a live full actor");
    *present=false;
    size_t bindings=qa_equipment_weapon_presentation_count(app->equipment,actor);
    for(size_t i=0;i<bindings;++i) {
        qa_weapon_presentation source;bool found=false;
        if(!qa_equipment_weapon_presentation_read(app->equipment,actor,i,&source,&found,error))return false;
        if(!found||!qa_equipment_weapon_presented(app->equipment,actor,source.provider))continue;
        application_q3_component_publication component={0};bool component_found=false;
        size_t components=application_q3_components_publication_count(app->components);
        for(size_t j=0;j<components;++j) {
            application_q3_component_publication row;
            if(!application_q3_components_publication_at(app->components,j,&row,error))return false;
            if(row.owner==source.provider){component=row;component_found=true;break;}
        }
        application_provider *qc=qc_item_provider(app,source.provider);
        if(!component_found&&!qc)continue;
        if(component_found&&(!component.game||!component.product||application_q3_component_items(component.game)!=source.context))
            return application_fail(error,QA_ERROR_ARGUMENT,"Source weapon presentation changed its actual component item owner");
        if(qc&&!application_qc_items_actor_current(qc->state.qc.engine,actor,error))return false;
        application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
        if(!primary||!primary->constructed||!primary->attached||primary->close_pending)
            return application_fail(error,QA_ERROR_NOT_FOUND,"Source weapon observation lost its actual primary world");
        qa_application_equipment_view view={.actor=actor,.provider=source.provider,.primary=primary->owner,
            .family=qc?qc->product->family:component.product->family,.selected=true,.source_slot=true,.source_binding=source,
            .item=source.active,.pending=source.pending,.pending_provider=source.pending?source.provider:0,
            .source_generation=qc?0:component.generation,.view_content=qc?qc->launch->content:component.content};
        if(source.active) {
            qa_item_admission definition;bool declared=false;
            bool read=qc?application_qc_items_item_read(qc,actor,source.active,&definition,
                &view.source_icon,&view.source_held,&declared,error):
                application_q3_mod_items_item_read(source.context,actor,source.active,&definition,
                &view.source_icon,&view.source_held,&declared,error);
            if(!read)return false;
            if(!declared||!definition.definition.weapon||definition.definition.owner!=source.provider)
                return application_fail(error,QA_ERROR_FORMAT,"Active source weapon is outside its genuine admitted declaration");
            view.label=definition.definition.label;view.ammo=definition.definition.ammo;
            view.has_weapon_status=view.has_start_requirement=true;
            if(view.ammo) {
                if(!qa_inventory_count_read(app->inventory,actor,view.ammo,&view.ammo_count,error))return false;
                view.finite_ammo=true;
            }
            view.has_ammo_to_start=!view.finite_ammo||view.ammo_count>0;
        }
        if(qc&&!qc_model(qc,actor,&view,error))return false;
        qa_equipment_state state;
        if(!qa_equipment_read(app->equipment,actor,&state))return false;
        if(state.weapon_slot.phase!=QA_WEAPON_SLOT_ACTIVE){view.pending=state.weapon_slot.next_item;view.pending_provider=state.weapon_slot.next_provider;}
        if(!view.pending)view.pending_provider=0;
        if(!q1_visibility(app,&view,error))return false;
        if(!qa_application_equipment_current(app,&view))
            return application_fail(error,QA_ERROR_ARGUMENT,"Source weapon observation retired its actual registry binding");
        *out=view;*present=true;return true;
    }
    return true;
}

bool qa_application_equipment_read(qa_application *app, qa_actor_id actor,
    qa_application_equipment_view *out, qa_error *error)
{
    bool source_present=false;
    if(!qa_application_equipment_source_read(app,actor,out,&source_present,error))return false;
    if(source_present)return true;
    application_equipment_gear_presentation gear;
    bool gear_selected = false;
    qa_equipment_state slot;
    if (app->equipment && qa_equipment_read(app->equipment, actor, &slot) &&
        (slot.weapon_slot_present?qa_equipment_weapon_presented(app->equipment,actor,slot.sources.grapple):slot.slot_active) &&
        slot.selection.grapple == QA_GRAPPLE_Q3 && slot.selection.binding == QA_EQUIPMENT_WEAPON_SLOT &&
        !application_equipment_gear_presentation_read(app, actor, &gear, &gear_selected, error)) return false;
    if (gear_selected) {
        qa_application_equipment_view view = {.actor = actor, .provider = gear.source.selected_owner,
            .primary = gear.primary, .gear_namespace = gear.source.gear_owner,
            .gear_service_owner = gear.source.service_owner, .family = QA_GAME_Q3,
            .item = gear.source.weapon_item, .label = "Grapple",
            .view_model = gear.source.definition->presentation.view_model,
            .q3_source = gear.gear.player, .has_q3_source = true,
            .q3_weapon = (qa_q3_weapon)gear.source.definition->presentation.weapon_index,
            .q3_time_ms = gear.gear.time_ms, .selected = true, .equipment_slot = true,
            .has_frame = true, .rate = 10,
            .has_weapon_status = true, .has_ammo_to_start = true};
        if (!view.item || !view.view_model || !view.view_model[0])
            return application_fail(error, QA_ERROR_NOT_FOUND,
                "Active gear slot has no actual admitted weapon identity or authored view model");
        if (!primary_visibility(app, actor, &view.visible, error)) return false;
        if (!qa_application_equipment_current(app, &view))
            return application_fail(error, QA_ERROR_ARGUMENT, "Gear equipment observation changed its actual slot or source");
        *out = view; return true;
    }
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending || !provider->product)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment observation has no actual selected arsenal");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!primary || !primary->constructed || !primary->attached || primary->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment observation has no authoritative primary source");
    qa_application_equipment_view view = {.actor = actor, .provider = provider->owner,
        .primary = primary->owner, .selected = provider != primary,
        .family = provider->product->family, .rate = 10};
    qa_q3_product q3_product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    bool standard_q3 = true, declared_source_catalog = false;
    double required = 1;
    double low_threshold = 0;
    bool q1 = provider->kind == APPLICATION_PROVIDER_Q1;
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view state;
        if (!qa_q1_player_read(provider->state.q1, actor, &state))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 equipment has no actual arsenal state");
        view.item = qa_q1_weapon_item(provider->state.q1, state.weapon);
        view.view_model = qa_strings_cstr(qa_session_strings(app->session), state.weapon_model);
        view.frame = state.weapon_frame; view.has_frame = true; view.kick_angles = state.punch_angles;
        view.visible = !state.holstered && view.view_model && view.view_model[0];
        view.has_start_requirement = true;
    } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        if (!qa_q2_weapon_read(provider->state.q2, actor, &state, error)) return false;
        const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(provider->state.q2, state.weapon);
        if (definition) {
            view.item = identity(app, definition->item);
            view.view_model = state.view_model ? qa_strings_cstr(qa_session_strings(app->session), state.view_model) : definition->view_model;
            required = definition->quantity;
            low_threshold = definition->warning;
            view.has_start_requirement = true;
        }
        view.frame = state.frame; view.has_frame = true; view.skin = state.view_skin; view.rate = state.gun_rate;
        view.has_skin = view.has_rate = true;
        qa_clock_state clock;
        if (!qa_session_clock(app->session, provider->owner, &clock))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 equipment has no actual source clock");
        uint64_t now = clock.frame.time_ns;
        float impulse = now == state.kick_ns ? 1 : 0;
        float factor = state.kick_seconds == 0 ? impulse : now >= state.kick_until_ns ? 0 :
            fminf(1, (float)((double)(state.kick_until_ns - now) / 1e9) / state.kick_seconds);
        bool rerelease = provider->product->edition == QA_EDITION_RERELEASE;
        view.kick_origin = qa_vec_scale(state.kick_origin, rerelease ? factor : impulse);
        view.kick_angles = qa_vec_scale(state.kick_angles, factor);
        view.visible = state.handoff != QA_Q2_PRIMARY_HOLSTERED && view.view_model && view.view_model[0];
    } else if (provider->kind == APPLICATION_PROVIDER_QC) {
        if (!application_guest_weapon_read(provider, actor, &view.item, error)) return false;
        if (!qa_application_equipment_current(app, &view))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC equipment changed its actual selected arsenal");
        if (!qc_model(provider, actor, &view, error)) return false;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE && view.family == QA_GAME_Q2) {
        qa_application_native_q2_equipment_view source;
        if (!application_q2_guest_equipment_read(provider, actor, &source, error)) return false;
        view.item = source.item; view.view_model = source.view_model; view.view_content = source.view_content;
        view.frame = source.frame; view.has_frame = true; view.skin = source.skin; view.rate = (float)source.rate;
        view.has_skin = source.has_skin; view.has_rate = source.has_rate;
        view.gun_origin = source.gun_offset; view.gun_angles = source.gun_angles;
        view.has_source_gun_pose = true; view.kick_angles = source.view_kick_angles;
        view.visible = source.visible;
    } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
        application_native_q3_equipment_view source;
        if (!application_native_q3_equipment_read(provider, actor, &source, error)) return false;
        view.q3_source = source.player; view.has_q3_source = true;
        view.q3_state = source.arsenal; view.has_q3_state = true;
        view.q3_time_ms = source.source_time_ms; view.q3_fire = source.fire;
        view.q3_weapon = (qa_q3_weapon)source.player.weapon; q3_product = source.player.product;
        view.item = qa_q3_weapon_item(provider->state.q3, view.q3_weapon, false);
        view.visible = view.q3_weapon != QA_Q3_W_NONE && view.q3_state.external_slot != QA_Q3_SLOT_HOLSTERED;
        view.has_start_requirement = true;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider); uint32_t client_slot;
        if (!engine || !engine->game || !application_q3_guest_actor_client(provider, actor, &client_slot) ||
            !qa_q3_host_source_player(engine->game->host, client_slot, &view.q3_source, error))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Source equipment has no actual begun GAME client");
        view.has_q3_source = true; view.source_weapon = view.q3_source.weapon;
        view.original_qvm = engine->game->vm != NULL;
        view.q3_time_ms = engine->milliseconds;
        if (engine->game->vm && !application_q3_guest_fire_read(provider, actor, &view.q3_fire, error)) return false;
        if (engine->game->catalog) {
            declared_source_catalog = true;
            const application_q3_catalog_weapon *weapons = NULL; size_t count = 0;
            if (!application_q3_catalog_role_current(engine->game->catalog, engine->game))
                return application_fail(error, QA_ERROR_ARGUMENT, "Original equipment lost its actual GAME catalog owner");
            if (!application_q3_catalog_weapons(engine->game->catalog, &weapons, &count, error)) return false;
            bool found = false;
            for (size_t i = 0; i < count; ++i) if (weapons[i].weapon == view.source_weapon) {
                if (found) return application_fail(error, QA_ERROR_FORMAT, "Original equipment repeats a source weapon identity");
                view.item = weapons[i].item; view.ammo = weapons[i].ammo; view.label = weapons[i].label; found = true;
            }
            if (view.source_weapon && !found)
                return application_fail(error, QA_ERROR_FORMAT, "Original equipment leaves its real source catalog");
            view.has_weapon_status = found;
            standard_q3 = application_q3_catalog_standard(engine->game->catalog) &&
                view.item == q3_identity(app, (qa_q3_weapon)view.source_weapon, false);
            if (standard_q3) {
                view.q3_weapon = (qa_q3_weapon)view.source_weapon;
                q3_product = engine->product;
            }
        } else {
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Original equipment has no declared source item namespace");
        }
        view.visible = view.source_weapon != 0; view.has_start_requirement = true;
    }
    if (standard_q3 && view.family == QA_GAME_Q3 && view.q3_weapon != QA_Q3_W_NONE) {
        if (!qa_q3_weapon_identity_name(view.q3_weapon) ||
            (q3_product == QA_Q3_ARENA && view.q3_weapon > QA_Q3_W_GRAPPLE) || !view.item)
            return application_fail(error, QA_ERROR_FORMAT, "Selected Q3 equipment leaves its actual admitted namespace");
        size_t count = 0; const qa_q3_item *items = qa_q3_items(q3_product, &count);
        for (size_t i = 0; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_WEAPON && items[i].tag == (int32_t)view.q3_weapon) {
                view.view_model = items[i].model; break;
            }
    }
    if (!view.original_qvm && !declared_source_catalog && !item_definition(app, &view, error)) return false;
    if (view.ammo) {
        if (!qa_inventory_count_read(app->inventory, actor, view.ammo, &view.ammo_count, error)) return false;
        view.finite_ammo = view.family != QA_GAME_Q3 || view.ammo_count != -1;
    }
    view.has_ammo_to_start = !view.finite_ammo ||
        (view.has_start_requirement && view.ammo_count >= required);
    if (q1 && view.item) {
        double owned;
        if (!qa_inventory_count_read(app->inventory, actor, view.item, &owned, error)) return false;
        view.has_ammo_to_start &= owned > 0;
    }
    view.low_ammo = view.family == QA_GAME_Q2 && view.finite_ammo && view.ammo_count <= low_threshold;
    if (standard_q3 && view.family == QA_GAME_Q3 && !q3_warning(app, &view, q3_product, error)) return false;
    if (!q1_visibility(app, &view, error)) return false;
    if (!qa_application_equipment_current(app, &view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment observation changed its actual actor or selected arsenal");
    *out = view; return true;
}
