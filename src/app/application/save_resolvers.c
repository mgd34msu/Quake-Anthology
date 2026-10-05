#include "save_private.h"
#include "guest_qc_items.h"
#include "guest_qc_pickups.h"
#include "guest_qc_combat.h"
#include "guest_qc_protection.h"
#include "guest_projection_private.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "native_q2_callbacks.h"
#include "native_q2_source_actors.h"
#include "qa/application_network.h"
#include "qa/frontend.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q1_inventory.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q3_save.h"
#include "qa/modes_save.h"
#include "guest_q3_combat.h"
#include "supplies.h"
#include "guest_q3_components.h"

static application_provider *source_owner(qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider && provider->attached && provider->constructed &&
            !provider->close_pending && provider->owner == owner)
            return provider;
    }
    return NULL;
}

static bool combat_binding(void *opaque, qa_actor_id actor, uint64_t serial,
                            qa_combat_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *combat = application_provider_for(app, actor, QA_ROLE_COMBAT, "");
    if (combat && combat->kind == APPLICATION_PROVIDER_QC)
        return application_qc_combat_saved_binding(combat->state.qc.engine,actor,serial,out,error);
    application_q3_component *component=application_q3_components_actor_owner(app,actor);
    if(component) return application_q3_component_combat_binding(component,actor,serial,out,error);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    if (provider && provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_combat_saved_binding(provider->state.qc.engine,actor,serial,out,error);
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    struct application_q3_guest *source = character ? q3g_engine(character) : NULL;
    if (source && source->game && source->game->combat)
        return application_q3_combat_binding(source->game->combat, actor, serial, out, error);
    if (provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine &&
        application_native_q2_source_actors_declared(provider->state.native.q2_engine))
        return application_native_q2_source_actors_combat_binding(provider->state.native.q2_engine,
            actor,serial,out,error);
    if (provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine)
        return application_native_q2_combat_binding(provider, actor, serial, out, error);
    struct application_q3_guest *guest = provider ? q3g_engine(provider) : NULL;
    if (guest && guest->game && guest->game->combat)
        return application_q3_combat_binding(guest->game->combat, actor, serial, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved combat primary has no restored source callback owner");
}

static bool admission(void *opaque, qa_actor_id actor,
                       qa_combat_admission *out, qa_error *error)
{
    qa_application *app = opaque;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_game_damage_admission(provider->state.q3, actor, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved damage admission has no restored native callback owner");
}

static bool protection(void *opaque,qa_actor_id actor,qa_protection_channel channel,
    const qa_protection_claim *claim,qa_protection_binding *out,qa_error *error)
{
    qa_application *app=opaque;
    if(!claim) return application_fail(error,QA_ERROR_ARGUMENT,"Saved protection has no actual source claim");
    application_provider *provider=source_owner(app,claim->owner);
    if(provider&&provider->kind==APPLICATION_PROVIDER_QC)
        return application_qc_protection_saved_binding(provider->state.qc.engine,actor,channel,claim,out,error);
    if(provider&&provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine&&
        provider->state.native.q2_engine->callbacks)
        return application_native_q2_callbacks_protection_saved_binding(provider->state.native.q2_engine,
            actor,channel,claim,out,error);
    application_q3_component_publication row;
    if(!application_q3_components_event_source_read(app,claim->owner,&row,error)) return false;
    return application_q3_mod_protection_saved_binding(application_q3_component_mod(row.game),actor,channel,claim,out,error);
}

static bool primary(void *opaque, qa_actor_id actor, uint64_t serial,
                    qa_inventory_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_INVENTORY, "");
    if (provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine)
        return application_native_q2_inventory_binding(provider, actor, serial, out, error);
    struct application_q3_guest *guest = provider ? q3g_engine(provider) : NULL;
    if (guest && guest->game)
        return application_guest_projection_inventory_binding(guest->game, actor,
                                                                serial, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved primary inventory has no restored source callback owner");
}

static bool inventory_group(void *opaque, qa_actor_id actor, uint64_t serial,
    const qa_inventory_source_group *saved, qa_inventory_items *out, qa_error *error)
{
    qa_application *app = opaque;
    if (!saved)
        return application_fail(error, QA_ERROR_ARGUMENT, "Saved inventory group is absent");
    qa_actor_owner modes_owner = qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)"application:modes", sizeof("application:modes") - 1});
    if (app->modes && modes_owner && saved->owner == modes_owner)
        return qa_modes_inventory_group(app->modes, actor, serial, saved, out, error);
    application_q3_component_publication component={0}; bool found=false;
    if(!application_q3_components_checkpoint_publication_read(app,saved->owner,&component,&found,error)) return false;
    if(found) return application_q3_component_inventory_group(component.game,actor,serial,saved,out,error);
    application_provider *provider = source_owner(app, saved->owner);
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_QC)
            return application_qc_items_saved_group(provider,actor,serial,saved,out,error);
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_inventory_group(provider->state.q1, actor, serial, saved, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            return qa_q2_game_inventory_group(provider->state.q2, actor, serial, saved, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q3)
            return qa_q3_game_inventory_group(provider->state.q3, actor, serial, saved, out, error);
        if(provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine&&provider->state.native.q2_engine->callbacks)
            return application_native_q2_callbacks_inventory_group(provider->state.native.q2_engine,actor,serial,saved,out,error);
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved inventory group has no restored source callback owner");
}

static bool pickup_observer(void *opaque, qa_actor_id actor, qa_actor_owner owner,
    uint64_t serial, qa_pickup_observer *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = source_owner(app, owner);
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_pickup_observer(provider->state.q1, actor, owner, serial, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            return qa_q2_game_pickup_observer(provider->state.q2, actor, owner, serial, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q3)
            return qa_q3_game_pickup_observer(provider->state.q3, actor, serial, out, error);
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved pickup observation has no restored source callback owner");
}

static bool pickup_rule(void *opaque,qa_actor_id actor,qa_actor_owner owner,
    uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider=source_owner(app,owner);
    if(provider&&provider->kind==APPLICATION_PROVIDER_QC)
        return application_qc_pickups_saved_rule(provider,actor,owner,serial,id,out,error);
    if(provider&&provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine&&
        provider->state.native.q2_engine->callbacks)
        return application_native_q2_callbacks_pickup_saved_rule(provider->state.native.q2_engine,
            actor,owner,serial,id,out,error);
    application_q3_component_publication component;
    bool found = false;
    if (!application_q3_components_checkpoint_publication_read(app, owner, &component, &found, error))
        return false;
    if (found)
        return application_q3_mod_pickup_saved_rule(application_q3_component_mod(component.game),
                                                   actor, owner, serial, id, out, error);
    return application_supplies_pickup_rule(app->supplies,actor,owner,serial,id,out,error);
}

static bool target(void *opaque, qa_actor_id actor, qa_clock_kind clock,
                     qa_target_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    bool ok = false;
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            ok = qa_q1_game_target_binding(provider->state.q1, actor, out, error);
        else if (provider->kind == APPLICATION_PROVIDER_Q2)
            ok = qa_q2_game_target_binding(provider->state.q2, actor, out, error);
        else if (provider->kind == APPLICATION_PROVIDER_Q3)
            ok = qa_q3_game_target_binding(provider->state.q3, actor, out, error);
    }
    if (ok && out->source == clock)
        return true;
    if (error && error->code != QA_OK)
        return false;
    return application_fail(error, QA_ERROR_FORMAT,
                            "Saved authored target has no matching source declaration");
}

bool application_save_resolvers(qa_application *app,
    qa_persistence_gameplay_resolvers *out, qa_error *error)
{
    if (!app || !app->session || !out || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Restored gameplay callbacks require retained application owners");
    *out = (qa_persistence_gameplay_resolvers){.context = app,
        .combat = combat_binding, .admission = admission, .protection=protection, .inventory_primary = primary,
        .inventory_group = inventory_group, .pickup_observer = pickup_observer, .pickup_rule = pickup_rule,
        .target = target};
    return true;
}
