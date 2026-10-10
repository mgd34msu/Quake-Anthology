#include "guest_native_q2_private.h"
#include "native_q2_console.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_combat.h"
#include <math.h>

typedef struct hud_selection {
    qa_application_native_q2_hud_source source;
    application_provider *hud, *character, *original;
    uint32_t slot;
} hud_selection;

static bool hud_select(qa_application *app, qa_actor_id actor, hud_selection *out, bool *found, qa_error *error)
{
    if (!app || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 HUD requires its actor and output");
    *found = false;
    if (!qa_actors_get(qa_session_actors(app->session), actor)) return true;
    application_provider *hud = application_provider_for(app, actor, QA_ROLE_HUD, "");
    if (!hud || !hud->constructed || hud->close_pending || !hud->product ||
        hud->product->family != QA_GAME_Q2) return true;
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    hud_selection selected = {.hud = hud, .character = character};
    qa_application_native_q2_hud_source *value = &selected.source;
    value->provider = value->data_provider = hud->owner;
    if (hud->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules rules;
        if (!hud->state.q2 || !qa_q2_combat_rules_read(hud->state.q2, &rules)) return true;
        value->edition = rules.edition;
        value->deathmatch = rules.deathmatch; value->cooperative = rules.cooperative;
    } else if (hud->kind == APPLICATION_PROVIDER_NATIVE) {
        struct application_native_q2 *engine = hud->state.native.q2_engine;
        if (!engine || (engine->profile != QA_NATIVE_Q2_GAME_API3 &&
            engine->profile != QA_NATIVE_Q2_GAME_API2023)) return true;
        value->edition = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
    } else return true;
    selected.original = hud->kind == APPLICATION_PROVIDER_NATIVE ? hud :
        character && character->product && character->product->family == QA_GAME_Q2 &&
        character->kind == APPLICATION_PROVIDER_NATIVE ? character : NULL;
    if (selected.original) {
        struct application_native_q2 *engine = selected.original->state.native.q2_engine;
        if (!engine || (engine->profile != QA_NATIVE_Q2_GAME_API3 &&
            engine->profile != QA_NATIVE_Q2_GAME_API2023)) return true;
        for (uint32_t i = 1; i < sizeof(engine->clients) / sizeof(engine->clients[0]); ++i)
            if (engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor, actor)) {
                selected.slot = i; break;
            }
        if (!selected.slot) return true;
        value->data_provider = selected.original->owner; value->config_revision = engine->config_revision;
        value->original = true; value->strings = qa_session_strings(app->session);
        value->configstrings = engine->configstrings;
        value->configstring_count = engine->configstring_count;
        value->edition = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
        value->statusbar = value->configstring_count > 5 && value->configstrings[5] ?
            qa_strings_cstr(value->strings, value->configstrings[5]) : "";
    } else {
        value->game = character && character->kind == APPLICATION_PROVIDER_Q2 ? character->state.q2 : hud->state.q2;
        value->data_provider = character && character->kind == APPLICATION_PROVIDER_Q2 ? character->owner : hud->owner;
        value->statusbar = qa_q2_wire_statusbar(hud->state.q2);
    }
    *out = selected; *found = true;
    return true;
}

bool qa_application_native_q2_hud_source_read(qa_application *app, qa_actor_id actor,
    qa_application_native_q2_hud_source *out, bool *found, qa_error *error)
{
    hud_selection selected; bool present;
    if (!out || !found) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 HUD requires its actor and output");
    if (!hud_select(app, actor, &selected, &present, error)) return false;
    *found = present;
    if (present) *out = selected.source;
    return true;
}

bool qa_application_native_q2_hud_read(qa_application *app, qa_actor_id actor,
    qa_application_native_q2_hud *out, bool *found, qa_error *error)
{
    hud_selection selected; bool present;
    if (!out || !found) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 HUD requires its actor and output");
    if (!hud_select(app, actor, &selected, &present, error)) return false;
    *found = false;
    if (!present) return true;
    const qa_application_native_q2_hud_source *source = &selected.source;
    application_provider *hud = selected.hud, *character = selected.character, *original = selected.original;
    qa_application_native_q2_hud value = {.provider = source->provider, .data_provider = source->data_provider,
        .config_revision = source->config_revision, .edition = source->edition,
        .deathmatch = source->deathmatch, .cooperative = source->cooperative, .original = source->original,
        .game = source->game, .statusbar = source->statusbar, .strings = source->strings,
        .configstrings = source->configstrings,
        .configstring_count = source->configstring_count, .layout = ""};
    application_provider *clock_owner = original ? original : hud;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, clock_owner->owner, &clock)) return true;
    value.time_ns = clock.frame.time_ns; value.server_frame = (int32_t)clock.frame_number;
    value.frame_ns = clock_owner->component.clock.interval_ns;
    value.cvars = application_native_q2_console_registry(clock_owner);
    value.hud_cvars = application_native_q2_console_hud_controls(clock_owner);
    if (original) {
        struct application_native_q2 *engine = original->state.native.q2_engine;
        qa_q2_player player;
        if (!qa_native_host_q2_player(original->state.native.host, selected.slot, &player, error)) return false;
        memcpy(value.stats, player.stats, sizeof(value.stats));
        if (player.stats[13] & 2)
            for (unsigned i = 0; i < sizeof(value.inventory) / sizeof(value.inventory[0]); ++i)
                value.inventory[i] = engine->clients[selected.slot].inventory[i];
        value.layout = engine->clients[selected.slot].layout;
        value.player_number = (int32_t)selected.slot - 1;
    } else {
        qa_q2_player_info player;
        bool native_character = character && character->kind == APPLICATION_PROVIDER_Q2;
        if (native_character) {
            if (!qa_q2_player_read(character->state.q2, actor, &player) || !player.connected) return true;
            qa_q2_wire_view retained;
            if (!qa_q2_wire_view_read(value.game, actor, &retained, error)) return false;
            value.view = retained.view;
            value.player_number = (int32_t)player.slot;
            if (!retained.present) {
                qa_combat_state combat;
                if (!qa_combat_read(app->combat, actor, &combat, error)) return false;
                value.view.health = combat.health; value.view.armor = combat.armor.regular.points;
                value.view.selected_item = player.selected_item;
                value.view.score = player.score; value.view.spectator = player.spectator;
            }
        } else {
            qa_combat_state combat;
            if (!qa_combat_read(app->combat, actor, &combat, error)) return false;
            value.view.health = combat.health; value.view.armor = combat.armor.regular.points;
            for (size_t i = 0; i < qa_q2_item_count(value.game); ++i) {
                const qa_q2_item_definition *item = qa_q2_item_at(value.game, i);
                if (combat.armor.regular.points > 0 && item->item == combat.armor.regular.item && item->icon)
                    value.view.armor_icon = qa_strings_find(qa_session_strings(app->session),
                        (qa_bytes){(const uint8_t *)item->icon, strlen(item->icon)});
            }
        }
        for (size_t i = 0; (value.view.layouts & 2) && i < qa_q2_item_count(value.game) &&
            i + 1 < sizeof(value.inventory) / sizeof(value.inventory[0]); ++i) {
            const qa_q2_item_definition *item = qa_q2_item_at(value.game, i);
            double count = 0;
            qa_error missing = {0};
            if (!qa_inventory_count_read(app->inventory, actor, item->item, &count, &missing) &&
                missing.code != QA_ERROR_NOT_FOUND) {
                if (error) *error = missing;
                return false;
            }
            value.inventory[i + 1] = count >= INT32_MIN && count < 2147483648.0 ? (int32_t)count : INT32_MIN;
        }
    }
    *out = value; *found = true;
    return true;
}

bool qa_application_native_q2_source_clock_read(qa_application *app, qa_actor_owner owner,
    qa_q2_edition *out, uint64_t *interval_ns, bool *found, qa_error *error)
{
    if (!app || !owner || !out || !interval_ns || !found || app->destroy_requested)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 Source profile requires its actual emitting owner");
    application_provider *provider=NULL;
    for (application_provider *p=app->live_providers;p;p=p->next_live)
        if (p->owner==owner) { provider=p; break; }
    *found=false;
    if (!provider || !provider->product || provider->product->family!=QA_GAME_Q2) return true;
    if (provider->application!=app || !provider->constructed || provider->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 Source profile names an unavailable GAME owner");
    qa_q2_edition edition;
    if (provider->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules rules;
        if (!provider->state.q2 || !qa_q2_combat_rules_read(provider->state.q2,&rules) ||
            rules.owner!=owner || (rules.edition!=QA_Q2_CLASSIC && rules.edition!=QA_Q2_RERELEASE))
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile lost its compiled GAME rules");
        edition=rules.edition;
    } else if (provider->kind==APPLICATION_PROVIDER_NATIVE) {
        const struct application_native_q2 *engine=provider->state.native.q2_engine;
        if (!engine || engine->provider!=provider)
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile lost its original GAME owner");
        if (engine->profile==QA_NATIVE_Q2_CGAME_API2023) return true;
        if (engine->profile!=QA_NATIVE_Q2_GAME_API3 && engine->profile!=QA_NATIVE_Q2_GAME_API2023)
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile has an unsupported original GAME ABI");
        edition=engine->profile==QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
    } else return true;
    const qa_clock_config *clock=&provider->component.clock;
    if (!clock->interval_ns || clock->kind!=(edition==QA_Q2_CLASSIC ?
            QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE))
        return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile lost its admitted frame interval");
    *out=edition; *interval_ns=clock->interval_ns; *found=true;
    return true;
}
bool qa_application_native_q2_source_profile_read(qa_application *app, qa_actor_owner owner,
    qa_q2_edition *out, bool *found, qa_error *error)
{
    uint64_t interval;
    return qa_application_native_q2_source_clock_read(app,owner,out,&interval,found,error);
}

static bool ready(const qa_application *app, bool retained)
{
    return app && app->session && app->world && !app->destroy_requested &&
        (app->state == QA_APPLICATION_RUNNING || app->state == QA_APPLICATION_STOPPING) && app->map_view_ready &&
        !app->q3_round_active && !app->q3_world_restart && !app->routing_snapshot &&
        !app->frame_preparing &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING ||
            (retained && app->operation == APPLICATION_PERSISTING && qa_application_content_graph_read(app))) &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) &&
        qa_world_idle(app->world);
}

static bool frame_equal(qa_source_frame left, qa_source_frame right, bool phase)
{
    return left.provider == right.provider && left.kind == right.kind &&
        (!phase || left.phase == right.phase) && left.number == right.number &&
        left.start_ns == right.start_ns && left.elapsed_ns == right.elapsed_ns &&
        left.time_ns == right.time_ns;
}

static bool observe(qa_application *app, bool retained,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    if (!out || !found || !ready(app, retained) ||
        (retained && !qa_application_content_graph_read(app)))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 presentation requires its completed published world source");
    *found = false;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || !provider->product || provider->product->family != QA_GAME_Q2)
        return true;
    if (provider->application != app || !provider->constructed || !provider->attached ||
        !provider->map_bound || provider->close_pending || !provider->launch ||
        !provider->launch->content || !provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 presentation lost its actual constructed physical source");
    qa_application_native_q2_presentation view = {.session = app->session,
        .publication = qa_application_launch(app), .launch = provider->launch,
        .content = provider->launch->content, .source_owner = provider->owner,
        .content_product = provider->launch->selection.product,
        .clock_config = provider->component.clock,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision,
        .retained = retained};
    if (!view.publication || !qa_session_clock(app->session, provider->owner, &view.clock) ||
        view.clock.frame.provider != provider->owner || view.clock.frame.phase != QA_FRAME_EXIT ||
        view.clock.frame.number != view.clock.frame_number ||
        (view.clock.frame.kind != QA_RULESET_Q2_CLASSIC && view.clock.frame.kind != QA_RULESET_Q2_RERELEASE) ||
        view.clock_config.kind != view.clock.frame.kind)
        return application_fail(error, QA_ERROR_FORMAT,
            "Q2 presentation has no matching completed physical source clock");
    if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules rules;
        bool intermission;
        uint64_t started;
        if (!provider->state.q2 || qa_q2_current_actor(provider->state.q2).registry ||
            !qa_q2_combat_rules_read(provider->state.q2, &rules) || rules.owner != provider->owner ||
            !qa_q2_bot_clock_read(provider->state.q2, &view.server_time_ns, &intermission, &started, error))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q2 presentation lost its idle physical GAME clock");
        view.kind = QA_APPLICATION_NATIVE_Q2_BUILTIN;
        view.source.game = provider->state.q2;
        view.edition = rules.edition;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE) {
        const struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (!engine || engine->provider != provider || engine->world != app->world ||
            !engine->initialized || !engine->map_ready || engine->shutting_down || engine->activation_failed ||
            (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023) ||
            !provider->state.native.host || !application_native_q2_idle(provider) ||
            qa_native_terminal(qa_native_host_instance(provider->state.native.host)) ||
            (view.clock.frame.number && !frame_equal(engine->frame, view.clock.frame, false)))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q2 presentation lost its completed original GAME invocation");
        view.kind = QA_APPLICATION_NATIVE_Q2_ORIGINAL;
        view.source.original.host = provider->state.native.host;
        view.source.original.profile = engine->profile;
        view.edition = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
        view.server_time_ns = view.clock.frame.time_ns;
    } else {
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Q2 presentation requires an actual compiled or original Q2 GAME");
    }
    if (view.server_time_ns != view.clock.frame.time_ns ||
        (view.edition == QA_Q2_CLASSIC ? QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE) != view.clock.frame.kind)
        return application_fail(error, QA_ERROR_FORMAT,
            "Q2 physical GAME time differs from its completed source frame");
    *out = view; *found = true;
    return true;
}

bool qa_application_native_q2_presentation_selected(qa_application *app,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    return observe(app, false, out, found, error);
}

bool qa_application_native_q2_presentation_retained_selected(qa_application *app,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    return observe(app, true, out, found, error);
}

static bool config_equal(qa_clock_config left, qa_clock_config right)
{
    return left.kind == right.kind && left.initial_time_ns == right.initial_time_ns &&
        left.interval_ns == right.interval_ns && left.minimum_frame_ns == right.minimum_frame_ns &&
        left.maximum_frame_ns == right.maximum_frame_ns && left.initial_lead_ns == right.initial_lead_ns &&
        left.maximum_steps == right.maximum_steps;
}

bool qa_application_native_q2_presentation_current(qa_application *app,
    const qa_application_native_q2_presentation *saved)
{
    qa_application_native_q2_presentation actual;
    bool found;
    if (!saved || !observe(app, saved->retained, &actual, &found, NULL) || !found ||
        actual.session != saved->session || actual.publication != saved->publication ||
        actual.launch != saved->launch || actual.content != saved->content ||
        actual.source_owner != saved->source_owner || actual.content_product != saved->content_product ||
        actual.edition != saved->edition || actual.kind != saved->kind ||
        actual.server_time_ns != saved->server_time_ns ||
        actual.publication_generation != saved->publication_generation || actual.map_revision != saved->map_revision ||
        !config_equal(actual.clock_config, saved->clock_config) ||
        actual.clock.host_origin_ns != saved->clock.host_origin_ns || actual.clock.elapsed_ns != saved->clock.elapsed_ns ||
        actual.clock.debt_ns != saved->clock.debt_ns || actual.clock.frame_number != saved->clock.frame_number ||
        actual.clock.paused != saved->clock.paused || !frame_equal(actual.clock.frame, saved->clock.frame, true))
        return false;
    return actual.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN ? actual.source.game == saved->source.game :
        actual.source.original.host == saved->source.original.host &&
        actual.source.original.profile == saved->source.original.profile;
}

bool qa_application_native_q2_presentation_extent(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t *out, qa_error *error)
{
    if (!out || !qa_application_native_q2_presentation_current(app,source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 prefix extent lost its completed GAME receipt");
    uint32_t extent;
    bool ok=source->kind==QA_APPLICATION_NATIVE_Q2_BUILTIN ?
        qa_q2_wire_extent(source->source.game,&extent,error) :
        qa_native_host_q2_wire_count((qa_native_host *)source->source.original.host,&extent,error);
    if (!ok) return false;
    if (!qa_application_native_q2_presentation_current(app,source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 prefix extent changed its GAME frame");
    *out=extent;
    return true;
}

bool qa_application_native_q2_presentation_entity(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t slot,
    qa_application_native_q2_entity_prefix *out, bool *found, qa_error *error)
{
    if (!out || !found || !qa_application_native_q2_presentation_current(app,source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 entity prefix lost its completed GAME receipt");
    qa_application_native_q2_entity_prefix value={.source_slot=slot};
    bool present;
    if (source->kind==QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        if (!qa_q2_wire_entity_read((qa_q2_game *)source->source.game,slot,&value.source.builtin,error)) return false;
        present=value.source.builtin.binding.in_use;
        value.actor=value.source.builtin.binding.actor;
    } else {
        if (!qa_native_host_q2_wire_entity((qa_native_host *)source->source.original.host,
                slot,&value.source.original,error)) return false;
        present=value.source.original.in_use && value.source.original.binding.kind!=QA_NATIVE_SLOT_FREE;
        value.actor=value.source.original.binding.actor;
    }
    if (!qa_application_native_q2_presentation_current(app,source) ||
        (present && !qa_actors_get(qa_session_actors(source->session),value.actor)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 entity prefix changed its physical frame or actor");
    *found=present;
    if (present) *out=value;
    return true;
}

static uint32_t source_solid_part(float value, uint32_t minimum, uint32_t maximum)
{
    if (value <= (float)minimum) return minimum;
    if (value >= (float)maximum) return maximum;
    return (uint32_t)value;
}

bool qa_application_native_q2_presentation_sample(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t slot,
    qa_application_native_q2_entity_sample *out, bool *found, qa_error *error)
{
    qa_application_native_q2_entity_prefix prefix;
    bool present;
    if (!out || !found)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 entity sample requires its Source output");
    if (!qa_application_native_q2_presentation_entity(app,source,slot,&prefix,&present,error)) return false;
    *found=false;
    if (!present) return true;
    qa_application_native_q2_entity_sample value={.actor=prefix.actor,.source_slot=slot};
    uint32_t solid=0;
    if (source->kind==QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        const qa_q2_entity *state=&prefix.source.original.state;
        value.origin=qa_v3(state->origin[0],state->origin[1],state->origin[2]);
        value.angles=qa_v3(state->angles[0],state->angles[1],state->angles[2]);
        value.previous_origin=qa_v3(state->old_origin[0],state->old_origin[1],state->old_origin[2]);
        value.models[0]=state->modelindex; value.models[1]=state->modelindex2;
        value.models[2]=state->modelindex3; value.models[3]=state->modelindex4;
        value.frame=state->frame; value.old_frame=state->old_frame; value.render_flags=state->renderfx;
        value.event=state->event; value.effects=state->effects;
        solid=state->solid;
    } else {
        const qa_q2_wire_source_entity *state=&prefix.source.builtin;
        value.origin=state->body.origin; value.angles=state->body.angles;
        value.previous_origin=state->previous_origin;
        if (state->has_visual) {
            memcpy(value.models,state->visual.models,sizeof(value.models));
            value.frame=state->visual.frame>=0 ? (uint32_t)state->visual.frame : 0;
            value.old_frame=state->visual.old_frame>=0 ? (uint32_t)state->visual.old_frame : value.frame;
            value.render_flags=state->visual.render_flags; value.effects=state->visual.effects;
        }
        value.event=state->event;
        if (!qa_vec_finite(state->body.bounds.mins) || !qa_vec_finite(state->body.bounds.maxs))
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source solid has nonfinite bounds");
        if (state->solid==QA_PHYSICS_BRUSH) solid=31;
        else if (state->solid==QA_PHYSICS_BOX && !(state->server_flags&2u)) {
            qa_bounds bounds=state->body.bounds;
            if (source->edition==QA_Q2_CLASSIC)
                solid=source_solid_part(bounds.maxs.x/8,1,31) |
                    (source_solid_part(-bounds.mins.z/8,1,31)<<5) |
                    (source_solid_part((bounds.maxs.z+32)/8,1,63)<<10);
            else if (bounds.mins.x!=bounds.maxs.x || bounds.mins.y!=bounds.maxs.y || bounds.mins.z!=bounds.maxs.z) {
                solid=source_solid_part(bounds.maxs.x,1,255) |
                    (source_solid_part(bounds.maxs.y,1,255)<<8) |
                    (source_solid_part(-bounds.mins.z,0,255)<<16) |
                    (source_solid_part(bounds.maxs.z+32,0,255)<<24);
                if (solid==31) solid=0;
            }
        }
    }
    if (!qa_vec_finite(value.origin) || !qa_vec_finite(value.angles))
        return application_fail(error,QA_ERROR_FORMAT,"Q2 Source sample has a nonfinite pose");
    if (solid && solid!=31) {
        float x,y,down,up;
        if (source->edition==QA_Q2_CLASSIC) {
            x=(float)(solid&31u)*8; y=x;
            down=(float)((solid>>5)&31u)*8; up=(float)((solid>>10)&63u)*8-32;
        } else {
            x=(float)(solid&255u); y=(float)((solid>>8)&255u);
            down=(float)((solid>>16)&255u); up=(float)((solid>>24)&255u)-32;
        }
        value.solid_bounds=(qa_bounds){qa_v3(-x,-y,-down),qa_v3(x,y,up)};
        value.solid_radius=qa_vec_length(qa_vec_sub(value.solid_bounds.maxs,value.solid_bounds.mins))*.5f;
    }
    *out=value; *found=true;
    return true;
}
