#include "guest_q3_weapons_services_private.h"
#include "guest_q3_catalog.h"
#include "guest_projection_private.h"
#include "guest_input_private.h"
#include "control_frame.h"
#include "supplies.h"
#include "equipment_runtime.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"

static qa_application *application(application_q3_weapons_services *s)
{ return s->role->engine->provider->application; }
static q3_weapon_actor_state *actor_state_find(const application_q3_weapons_services *s,
    qa_actor_id actor)
{
    size_t slot;
    if (!qa_actor_index_find_retained(&s->actor_states, actor, &slot)) return NULL;
    q3_weapon_storage *storage = qa_pool_at(&s->state_pool, slot);
    return &storage->actor;
}
static q3_weapon_storage *state_take(application_q3_weapons_services *s,
    size_t *slot, qa_error *error)
{
    if (s->state_pool.active == s->state_pool.capacity &&
        !qa_pool_extend(&s->state_pool, &s->state_storage, s->state_pool.capacity, error))
        return NULL;
    return qa_pool_take(&s->state_pool, slot);
}
static q3_weapon_actor_state *actor_state_take(application_q3_weapons_services *s,
    qa_actor_id actor, qa_error *error)
{
    q3_weapon_actor_state *state = actor_state_find(s, actor);
    if (state) return state;
    size_t slot;
    q3_weapon_storage *storage = state_take(s, &slot, error);
    if (!storage) return NULL;
    state = &storage->actor;
    *state = (q3_weapon_actor_state){.actor = actor, .storage_slot = slot};
    if (!qa_actor_index_remember(&s->actor_states, actor, slot, error)) {
        qa_pool_release(&s->state_pool, slot); return NULL;
    }
    return state;
}
static void actor_state_collect(application_q3_weapons_services *s,
    q3_weapon_actor_state *state)
{
    if (state->requested || state->matches) return;
    qa_actor_index_forget_retained(&s->actor_states, state->actor);
    qa_pool_release(&s->state_pool, state->storage_slot);
}
static void request_remove(application_q3_weapons_services *s, q3_weapon_actor_state *state)
{
    if (!state || !state->requested) return;
    if (state->request_previous) state->request_previous->request_next = state->request_next;
    else s->first_request = state->request_next;
    if (state->request_next) state->request_next->request_previous = state->request_previous;
    else s->last_request = state->request_previous;
    state->request_previous = state->request_next = NULL;
    state->requested = false; --s->request_count;
    actor_state_collect(s, state);
}
void q3_weapon_requests_clear(application_q3_weapons_services *s)
{ while (s->first_request) request_remove(s, s->first_request); }
bool q3_weapon_request_store(application_q3_weapons_services *s, qa_actor_id actor,
    int32_t weapon, qa_error *error)
{
    q3_weapon_actor_state *state = actor_state_take(s, actor, error);
    if (!state) return false;
    if (!state->requested) {
        state->request_previous = s->last_request;
        if (s->last_request) s->last_request->request_next = state;
        else s->first_request = state;
        s->last_request = state; state->requested = true; ++s->request_count;
    }
    state->weapon = weapon; return true;
}
bool q3_weapon_services_source(application_q3_weapons_services *s, qa_actor_id actor,
    q3_weapon_actor *out, qa_error *error)
{
    if (!s || !s->role || s->role->retired || s->role->kind != QA_QVM_GAME ||
        s->role->image != s->image || s->role->vm != s->vm || s->role->abi != s->abi ||
        s->role->engine->game != s->role || s->role->weapon_services != s || !s->role->weapons ||
        (s->role->engine->provider->close_pending && !s->closing_match))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon services lost their physical GAME owner");
    qa_application *app = application(s);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *physical = s->role->engine->provider;
    if (!record || !q3_weapons_actor(s->role->weapons, actor, out, error)) return false;
    uint32_t slot = out->slot;
    if (record->owner == physical->owner && (!record->has_source || record->source_slot != slot))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon source actor lost its actual owned slot");
    if (slot >= sizeof(s->role->engine->clients) / sizeof(*s->role->engine->clients) ||
        !s->role->engine->clients[slot].connected ||
        !qa_actor_id_equal(s->role->engine->clients[slot].actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon service actor differs from its physical client");
    return true;
}
static bool match_current(q3_weapon_match *match, q3_weapon_actor *source, qa_error *error)
{
    application_q3_weapons_services *s = match->services;
    qa_application *app = application(s);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), match->actor);
    return record && record->owner == match->source_owner && app->primary_mode_ready &&
        app->primary_mode.slot == match->mode.slot && app->primary_mode.generation == match->mode.generation &&
        q3_weapon_services_source(s, match->actor, source, error) ? true :
        (error && error->code != QA_OK) ? false :
            application_fail(error, QA_ERROR_NOT_FOUND, "Original match player lost its full actor and mode owner");
}
static bool match_score(void *context, int32_t *out, qa_error *error)
{
    q3_weapon_match *match = context; application_q3_weapons_services *s = match->services;
    q3_weapon_actor source;
    if (!out || !match_current(match, &source, error)) return false;
    ++s->calls;
    bool ok = application_q3_weapons_score(s->role->weapons, match->actor, out, error);
    --s->calls; return ok;
}
static bool match_set_score(void *context, int32_t value, qa_error *error)
{
    q3_weapon_match *match = context; application_q3_weapons_services *s = match->services;
    q3_weapon_actor source;
    if (s->closing_match)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original match score mutation lost its current owner");
    if (!match_current(match, &source, error)) return false;
    ++s->calls;
    bool ok = application_q3_weapons_set_score(s->role->weapons, match->actor, value, error) &&
        match_current(match, &source, error);
    --s->calls; return ok;
}
static bool match_team(void *context, qa_team_id *out, qa_error *error)
{
    q3_weapon_match *match = context; application_q3_weapons_services *s = match->services;
    q3_weapon_actor source; qa_combat_state state;
    if (!out || !match_current(match, &source, error)) return false;
    ++s->calls;
    bool ok = qa_combat_read(application(s)->combat, match->actor, &state, error) &&
        match_current(match, &source, error);
    if (ok) {
        if (state.team.kind == QA_TEAM_NUMBER) {
            const application_q3_weapon_profile *profile = &s->role->weapons->profile;
            ok = false;
            for (size_t i = 0; i < profile->team_count; ++i)
                if (qa_team_equal(qa_session_strings(application(s)->session), state.team,
                    qa_team_named(profile->teams[i].team))) { *out = profile->teams[i].team; ok = true; break; }
            if (!ok) application_fail(error, QA_ERROR_ARGUMENT, "Shared team has no declared original command");
        } else *out = state.team.kind == QA_TEAM_NAMED ? state.team.name : 0;
    }
    --s->calls; return ok;
}
static bool match_set_team(void *context, qa_team_id team, qa_error *error)
{
    q3_weapon_match *match = context; application_q3_weapons_services *s = match->services;
    q3_weapon_actor source; qa_combat_state current;
    if (s->closing_match)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original match team mutation lost its current owner");
    if (!match_current(match, &source, error)) return false;
    ++s->calls;
    bool current_ok = qa_combat_read(application(s)->combat, match->actor, &current, error) &&
        match_current(match, &source, error);
    --s->calls;
    if (!current_ok) return false;
    if (qa_team_equal(qa_session_strings(application(s)->session), current.team, qa_team_named(team))) return true;
    const application_q3_weapon_team_command *command;
    if (!application_q3_weapons_team_command(s->role->weapons, match->actor, team, &command, error)) return false;
    if (command->argument_count > SIZE_MAX / sizeof(const char *))
        return application_fail(error, QA_ERROR_MEMORY, "Original team argument vector exceeds native extent");
    const char **arguments = malloc(command->argument_count * sizeof(*arguments));
    if (!arguments) return application_fail(error, QA_ERROR_MEMORY, "Retaining exact original team argument vector");
    qa_strings *strings = qa_session_strings(application(s)->session);
    for (size_t i = 0; i < command->argument_count; ++i)
        arguments[i] = qa_strings_cstr(strings, command->arguments[i]);
    ++s->calls;
    bool ok = application_q3_guest_client_command_vector(s->role->engine->provider, match->actor,
        arguments, command->argument_count, error) && match_current(match, &source, error);
    --s->calls; free(arguments); return ok;
}
bool application_q3_weapons_services_match_binding(application_q3_weapons_services *s,
    qa_mode_id mode, qa_actor_id actor, qa_actor_owner owner, qa_match_binding *out, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !q3_weapon_services_source(s, actor, &source, error) ||
        !s->role->weapons->profile.has_match)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original match binding has no retained source declaration");
    qa_application *app = application(s);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    if (!record || record->owner != owner || !app->primary_mode_ready ||
        mode.slot != app->primary_mode.slot || mode.generation != app->primary_mode.generation)
        return application_fail(error, QA_ERROR_FORMAT, "Original match binding differs from its actual actor and primary mode");
    q3_weapon_actor_state *state = actor_state_take(s, actor, error);
    if (!state) return false;
    q3_weapon_match *match = state->matches;
    while (match && (match->mode.slot != mode.slot || match->mode.generation != mode.generation))
        match = match->actor_next;
    if (!match) {
        size_t slot;
        q3_weapon_storage *storage = state_take(s, &slot, error);
        if (!storage) { actor_state_collect(s, state); return false; }
        match = &storage->match;
        *match = (q3_weapon_match){.next = s->matches, .actor_next = state->matches,
            .services = s, .actor_state = state, .storage_slot = slot, .actor = actor,
            .source_owner = owner, .mode = mode};
        s->matches = state->matches = match;
    }
    if (match->source_owner != owner)
        return application_fail(error, QA_ERROR_FORMAT, "Original match callback owner changed its actor namespace");
    *out = (qa_match_binding){.owner = owner, .context = match, .score = match_score,
        .set_score = match_set_score, .team = match_team, .set_team = match_set_team};
    return true;
}
bool application_q3_weapons_services_match_admit(application_q3_weapons_services *s,
    qa_actor_id actor, qa_error *error)
{
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    qa_application *app = application(s);
    if (!s->role->weapons->profile.has_match || !app->primary_mode_ready) return true;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    qa_match_binding binding; qa_match_lease lease; bool present;
    return application_q3_weapons_services_match_binding(s, app->primary_mode, actor, record->owner, &binding, error) &&
        qa_modes_player_lease(app->modes, app->primary_mode, actor, binding.owner, binding.context, &lease, &present, error) &&
        (present || qa_modes_bind_player(app->modes, app->primary_mode, actor, &binding, &lease, error));
}
bool application_q3_weapons_services_match_close(application_q3_weapons_services *s, qa_error *error)
{
    if (!s) return true;
    if (!application_q3_weapons_services_idle(s))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original match cleanup requires its idle Source owner");
    qa_application *app = application(s);
    s->closing_match = true;
    while (s->matches) {
        q3_weapon_match *match = s->matches; qa_match_lease lease; bool present;
        bool ok = qa_modes_player_lease(app->modes, match->mode, match->actor, match->source_owner,
            match, &lease, &present, error);
        if (ok && present) ok = qa_modes_unbind_player(app->modes, lease, error) &&
            qa_modes_player_lease(app->modes, match->mode, match->actor, match->source_owner,
                match, &lease, &present, error);
        if (!ok || present) {
            s->closing_match = false;
            return ok ? application_fail(error, QA_ERROR_ARGUMENT, "Original match actor retirement still retains its real binding") : false;
        }
        q3_weapon_actor_state *state = match->actor_state;
        q3_weapon_match **link = &state->matches;
        while (*link != match) link = &(*link)->actor_next;
        *link = match->actor_next;
        s->matches = match->next;
        qa_pool_release(&s->state_pool, match->storage_slot);
        actor_state_collect(s, state);
    }
    s->closing_match = false; return true;
}
static bool selected_arsenal(application_q3_weapons_services *s, qa_actor_id actor,
    application_provider **out, bool *present, qa_error *error)
{
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    qa_application *app = application(s);
    application_provider *p = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    *out = p; *present = false;
    if (!p || p == s->role->engine->provider) return true;
    if (!p->constructed || !p->attached || p->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected arsenal grant lost its current publication");
    *present = true;
    return true;
}
static bool selected(void *context, qa_actor_id actor)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, NULL)) return false;
    qa_application *app = application(s);
    return application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == s->role->engine->provider &&
        (!app->equipment || qa_equipment_primary_selected(app->equipment, actor));
}
static bool attempted(void *context, qa_actor_id actor, int32_t weapon, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    q3_weapon_actor_state *state = actor_state_find(s, actor);
    if (state && state->requested && state->weapon != weapon) request_remove(s, state);
    return true;
}
static bool accepted(void *context, qa_actor_id actor, int32_t weapon, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (weapon < 0) return application_fail(error, QA_ERROR_FORMAT, "Accepted Source weapon selection is negative");
    return q3_weapon_services_source(s, actor, &source, error) && q3_weapon_request_store(s, actor, weapon, error);
}
bool application_q3_weapons_services_select_intent(application_q3_weapons_services *s,
    qa_actor_id actor, qa_actor_owner owner, qa_item_id item, bool *admitted, qa_error *error)
{
    q3_weapon_actor source;
    if (!owner || !item || !admitted)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon selection requires its actual owner and canonical item");
    *admitted = false;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    qa_application *app = application(s);
    application_provider *provider = s->role->engine->provider;
    if (provider->owner != owner || !provider->constructed || !provider->attached || provider->close_pending ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon selection changed its actual selected arsenal");
    const application_q3_weapon_profile *profile = application_q3_weapons_profile(s->role->weapons);
    size_t index = 0;
    while (index < profile->catalog_count && profile->catalog[index].item != item) ++index;
    if (index == profile->catalog_count) return true;
    int32_t weapon = profile->catalog[index].weapon;
    if (s->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Original weapon selection nesting exceeds native extent");
    ++s->calls;
    qa_item_id active; double count = 0;
    bool ok = application_q3_weapons_active(s->role->weapons, actor, &active, error);
    if (ok && active != item) ok = qa_inventory_count_read(app->inventory, actor, item, &count, error);
    if (ok && (!q3_weapons_current(s->role->weapons, &source) ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider ||
        provider->owner != owner || !provider->constructed || !provider->attached || provider->close_pending))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon selection replaced its real actor or inventory owner");
    if (ok && (active == item || count > 0)) {
        if (active != item) ok = q3_weapon_request_store(s, actor, weapon, error);
        if (ok) *admitted = true;
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_request(application_q3_weapons_services *s, qa_actor_id actor,
    int32_t *out, bool *present, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !present || !q3_weapon_services_source(s, actor, &source, error)) return false;
    q3_weapon_actor_state *state = actor_state_find(s, actor);
    *present = state && state->requested;
    if (*present) *out = state->weapon;
    return true;
}
bool application_q3_weapons_services_request_completed(application_q3_weapons_services *s,
    qa_actor_id actor, qa_error *error)
{
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    q3_weapon_actor_state *state = actor_state_find(s, actor);
    if (!state || !state->requested) return true;
    int32_t selection;
    if (!q3_weapons_read(s->role->weapons, source.player +
        s->role->weapons->profile.selection_offset, &selection, error)) return false;
    if (selection == state->weapon) request_remove(s, state);
    return true;
}
void application_q3_weapons_services_actor_released(application_q3_weapons_services *s, qa_actor_record actor)
{ if (s) request_remove(s, actor_state_find(s, actor.id)); }
bool application_q3_weapons_services_slice_finish(q3g_role *role, qa_actor_id actor,
    const qa_usercmd *command, const qa_q3_player *player, bool reached, qa_error *error)
{
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    q3_weapon_actor source;
    if (!command || !player || !q3_weapon_services_source(s, actor, &source, error)) return false;
    bool available;
    if (!application_q3_weapons_available(role->weapons, actor, true, &available, error)) return false;
    if (!available) return true;
    ++s->calls;
    bool ok = application_control_guest_weapon_step(application(s), actor, command, player, reached, error);
    if (ok && !q3_weapons_current(s->role->weapons, &source))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected weapon slice replaced its original Source player");
    --s->calls; return ok;
}
static bool selected_original(void *context, qa_actor_id actor, uint32_t selection,
    application_q3_weapons_services **out, struct application_native_q2 **native,
    bool *present, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending || !provider->state.q3 || !out || !present)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 effect lost its real native provider");
    *out = NULL; *present = false;
    if (native) *native = NULL;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || (physical->kind != APPLICATION_PROVIDER_QVM &&
        !(native && physical->kind == APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine))) return true;
    qa_q3_player_state player;
    if (!qa_q3_player_read(provider->state.q3, actor, &player) ||
        !(((selection & player.selections & QA_Q3_ARSENAL) &&
            application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider) ||
          ((selection & player.selections & QA_Q3_EQUIPMENT) &&
            application_provider_for(app, actor, QA_ROLE_EQUIPMENT, "") == provider)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 effect differs from its actual actor role");
    if (physical->kind == APPLICATION_PROVIDER_NATIVE) {
        *native = physical->state.native.q2_engine; *present = true; return true;
    }
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    *out = s; *present = true; return true;
}
bool application_q3_weapons_services_grenade_interval(void *context, qa_actor_id actor,
    qa_actor_owner owner, uint64_t native, uint64_t *out, bool *handled, qa_error *error)
{
    qa_application *app = context;
    if (!app || !app->session || !out || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hand grenade cadence has no actual application");
    *handled = false;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    bool native_q2 = physical && physical->kind == APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine;
    if (!physical || (!native_q2 && physical->kind != APPLICATION_PROVIDER_QVM)) return true;
    qa_equipment_state equipment;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &equipment) ||
        !equipment.selection.grenades.enabled || equipment.sources.grenades != owner ||
        !application_equipment_runtime_owner_current(app->equipment_runtime, owner))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its actual selected equipment");
    application_provider *provider = NULL;
    qa_id_index_cursor cursor = application_provider_lookup_begin(app, owner,
        APPLICATION_PROVIDER_PUBLISHED);
    application_provider *candidate;
    while ((candidate = application_provider_lookup_next(app, &cursor))) provider = candidate;
    qa_q2_hand_grenade_state grenade; bool bound = false;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->state.q2)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its native provider");
    qa_q2_game *game = provider->state.q2;
    if (!qa_q2_hand_grenade_read(game, actor, &grenade, &bound, error)) return false;
    if (!bound)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its native action owner");
    if (native_q2) {
        qa_item_id item;
        return application_native_q2_attack_hand_grenade_item(physical->state.native.q2_engine, &item, error) &&
            application_native_q2_attack_delay(physical->state.native.q2_engine, actor, item, native, out, handled, error);
    }
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    uint64_t milliseconds = native / UINT64_C(1000000);
    if (native % UINT64_C(1000000) >= UINT64_C(500000)) ++milliseconds;
    if (milliseconds > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hand grenade cadence exceeds its original int32 interval");
    /* This identifier names the actual native Q2 hand-action implementation,
     * whose retained provider and admitted actor were qualified above. */
    qa_string_id implementation;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "q2:equipment/hand-grenades",
        &implementation, error)) return false;
    ++s->calls;
    int32_t result;
    bool ok = application_q3_weapons_equipment_delay(role->weapons, actor, implementation,
        (int32_t)milliseconds, &result, error);
    if (ok && result < 0)
        ok = application_fail(error, QA_ERROR_FORMAT, "Original hand grenade cadence produced a negative interval");
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        !provider->constructed || !provider->attached || provider->close_pending || provider->state.q2 != game ||
        !qa_equipment_read(app->equipment, actor, &equipment) || equipment.sources.grenades != owner ||
        !application_equipment_runtime_owner_current(app->equipment_runtime, owner)))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence replaced its original or selected owner");
    if (ok) { *out = (uint64_t)result * UINT64_C(1000000); *handled = true; }
    --s->calls; return ok;
}
bool application_q3_weapons_services_equipment_animation(void *context, qa_actor_id actor,
    bool reverse, bool melee, bool *handled, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || !provider->application || !provider->constructed || !provider->attached ||
        provider->close_pending || !handled)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment animation lost its native source");
    *handled = false;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    bool native_q2 = physical && physical->kind == APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine;
    if (!physical || (!native_q2 && physical->kind != APPLICATION_PROVIDER_QVM)) return true;
    qa_equipment_state equipment;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &equipment) ||
        equipment.selection.binding != QA_EQUIPMENT_WEAPON_SLOT ||
        equipment.sources.grapple != provider->owner ||
        !(equipment.slot_active || equipment.slot_lowering) ||
        !qa_equipment_weapon_presented(app->equipment, actor, provider->owner)) return true;
    if (!application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment animation retired its retained source");
    if (native_q2) {
        if (reverse) { *handled = true; return true; }
        return application_native_q2_attack_animation(physical->state.native.q2_engine, actor, handled, error);
    }
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    *handled = true;
    if (reverse) return true;
    ++s->calls;
    bool ok = application_q3_weapons_animation(role->weapons, actor, melee, error);
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        !qa_equipment_read(app->equipment, actor, &equipment) ||
        equipment.sources.grapple != provider->owner ||
        !application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner)))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Equipment animation replaced its original or selected owner");
    --s->calls; return ok;
}
bool application_q3_weapons_services_grapple_frame(void *context, qa_actor_id actor,
    int32_t frame, qa_error *error)
{
    if (frame != 2) return true;
    bool handled;
    return application_q3_weapons_services_equipment_animation(context, actor, false, true,
        &handled, error);
}
bool application_q3_weapons_services_selected_fired(void *context, qa_actor_id actor,
    qa_item_id weapon, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || !provider->application || !provider->constructed || !provider->attached ||
        provider->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected attack animation lost its native arsenal");
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    bool native_q2 = physical && physical->kind == APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine;
    if (!physical || physical == provider || (!native_q2 && physical->kind != APPLICATION_PROVIDER_QVM)) return true;
    if (application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    if (app->equipment && !qa_equipment_primary_selected(app->equipment, actor)) return true;
    if (native_q2) {
        bool handled;
        return application_native_q2_attack_animation(physical->state.native.q2_engine, actor, &handled, error);
    }
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    bool melee = provider->kind == APPLICATION_PROVIDER_Q1 && weapon &&
        (weapon == qa_q1_weapon_item(provider->state.q1, QA_Q1_AXE) ||
         weapon == qa_q1_weapon_item(provider->state.q1, QA_Q1_MJOLNIR));
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        melee = weapon && weapon == qa_q3_weapon_item(provider->state.q3, QA_Q3_W_GAUNTLET, false);
    ++s->calls;
    bool ok = application_q3_weapons_animation(role->weapons, actor, melee, error);
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider ||
        !provider->constructed || !provider->attached || provider->close_pending))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected attack animation replaced its actual Source or arsenal");
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_delay(void *context, qa_actor_id actor,
    uint64_t native, uint64_t *out, bool *handled, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || !provider->application || !provider->constructed || !provider->attached ||
        provider->close_pending || !out || !handled)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected cadence lost its native arsenal");
    *handled = false;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical == provider ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    if (physical->kind == APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine) {
        qa_item_id item;
        return qa_application_weapon_read(app, actor, &item, error) &&
            application_native_q2_attack_delay(physical->state.native.q2_engine, actor, item, native, out, handled, error);
    }
    if (physical->kind != APPLICATION_PROVIDER_QVM) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    uint64_t milliseconds = native / UINT64_C(1000000);
    if (milliseconds > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected cadence exceeds its original int32 interval");
    ++s->calls;
    int32_t result;
    bool ok = application_q3_weapons_delay(role->weapons, actor, (int32_t)milliseconds, &result, error);
    if (ok && result < 0)
        ok = application_fail(error, QA_ERROR_FORMAT, "Original selected cadence produced a negative interval");
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider ||
        !provider->constructed || !provider->attached || provider->close_pending))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected cadence replaced its original or arsenal owner");
    if (ok) { *out = (uint64_t)result * UINT64_C(1000000); *handled = true; }
    --s->calls; return ok;
}
bool application_q3_weapons_services_firing_delay(void *context, qa_actor_id actor,
    int32_t milliseconds, int32_t *out, qa_error *error)
{
    if (milliseconds < 0 || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected firing cadence needs a nonnegative interval");
    uint64_t interval; bool handled;
    if (!application_q3_weapons_services_selected_delay(context, actor,
        (uint64_t)milliseconds * UINT64_C(1000000), &interval, &handled, error)) return false;
    if (!handled) { *out = milliseconds; return true; }
    interval /= UINT64_C(1000000);
    if (interval > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Selected firing cadence exceeds its millisecond domain");
    *out = (int32_t)interval; return true;
}
bool application_q3_weapons_services_q2_damage(void *context, qa_actor_id actor,
    float *out, bool *handled, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->state.q2 || !out || !handled)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 damage lost its native weapon owner");
    *handled = false;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    qa_q2_game *game = provider->state.q2;
    bool arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider;
    bool admitted = arsenal;
    qa_equipment_state equipment = {0};
    if (!admitted && app->equipment && qa_equipment_read(app->equipment, actor, &equipment))
        admitted = (equipment.selection.grenades.enabled && equipment.sources.grenades == provider->owner) ||
            ((equipment.selection.grapple == QA_GRAPPLE_Q2_CTF || equipment.selection.grapple == QA_GRAPPLE_LMCTF) &&
                equipment.sources.grapple == provider->owner);
    if (!admitted) return true;
    if (!arsenal && !application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 damage lost its actual equipment controller");
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    ++s->calls;
    bool ok = application_q3_weapons_damage_factor(role->weapons, actor, out, error);
    qa_equipment_state after;
    bool current = ok && (arsenal ? application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider :
        qa_equipment_read(app->equipment, actor, &after) &&
        after.selection.grenades.enabled == equipment.selection.grenades.enabled &&
        after.sources.grenades == equipment.sources.grenades &&
        after.selection.grapple == equipment.selection.grapple &&
        after.sources.grapple == equipment.sources.grapple &&
        application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner));
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        !current || provider->state.q2 != game || !provider->constructed ||
        !provider->attached || provider->close_pending))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 damage replaced its actual Source or weapon owner");
    if (ok) *handled = true;
    --s->calls; return ok;
}
bool application_q3_weapons_services_q2_powerups(void *context, qa_actor_id actor,
    qa_builtin_powerups *out, bool *handled, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->state.q2 || !out || !handled)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 powers lost their native weapon owner");
    *handled = false;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    bool admitted = application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider;
    qa_equipment_state equipment;
    if (!admitted && app->equipment && qa_equipment_read(app->equipment, actor, &equipment))
        admitted = equipment.selection.grenades.enabled && equipment.sources.grenades == provider->owner &&
            application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner);
    if (!admitted) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    if (role && !role->weapon_services) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(role ? role->weapon_services : NULL, actor, &source, error)) return false;
    /* The original QVM owns these powers inside its damage and delay calls.
     * Both genuine selected Q2 and offhand grenade inputs declare zero timers. */
    *out = (qa_builtin_powerups){0};
    *handled = true;
    return true;
}
bool application_q3_weapons_services_q1_damage(void *context, qa_damage_request *request,
    qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->state.q1 || !request)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q1 damage lost its actual native Source owner");
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM ||
        !request->attack.attacker.registry || !request->attack.weapon ||
        request->attack.weapon_provider != provider->owner) return true;
    bool declared = false;
    for (qa_q1_weapon weapon = QA_Q1_AXE; weapon < QA_Q1_WEAPON_COUNT; ++weapon)
        if (qa_q1_weapon_item(provider->state.q1, weapon) == request->attack.weapon) { declared = true; break; }
    if (!declared)
        return application_fail(error, QA_ERROR_FORMAT, "Selected Q1 damage leaves its actual weapon declaration");
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    if (role && !s) return true;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, request->attack.attacker, &source, error)) return false;
    qa_q1_game *game = provider->state.q1;
    ++s->calls;
    float factor;
    bool ok = application_q3_weapons_damage_factor(role->weapons, request->attack.attacker, &factor, error);
    if (ok && (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        provider->state.q1 != game || !provider->constructed || !provider->attached || provider->close_pending))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q1 damage replaced its held physical Source or native owner");
    if (ok) {
        request->amount *= factor;
        request->knockback = request->amount;
        request->attack.powerup_applied = true;
        request->attack.powerup_owner = physical->owner;
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_q2_input(void *context, qa_actor_id actor,
    qa_q2_weapon_input *input, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending || !provider->state.q2 || !input)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 input lost its actual arsenal owner");
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    q3_weapon_actor source; qa_q3_player player; qa_body_state body;
    if (role && !s) return true;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    bool available, attacking;
    if (!application_q3_weapons_available(role->weapons, actor, false, &available, error) ||
        !application_q3_weapons_available(role->weapons, actor, true, &attacking, error) ||
        !qa_qvm_read_player(role->vm, (int32_t)source.player, true, &player, error) ||
        !qa_world_body_read(app->world, actor, &body, error)) return false;
    bool pressed = input->attack;
    if (!application_guest_input_weapon_slice(app, actor)) {
        qa_q2_weapon_turn_state turn;
        if (!qa_q2_weapon_turn_read(provider->state.q2, actor, &turn, error)) return false;
        pressed = turn.attack;
    }
    bool primary = !app->equipment || qa_equipment_primary_selected(app->equipment, actor);
    if (!q3_weapons_current(role->weapons, &source) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != physical ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q2 input replaced its physical Source player");
    input->angles = qa_v3(player.viewangles[0], player.viewangles[1], player.viewangles[2]);
    input->view_height = (float)player.viewheight;
    input->gravity = app->physics->gravity;
    input->attack = attacking && pressed && primary;
    input->latched_attack = input->holster = input->latched_holster = false;
    input->ducked = body.bounds.maxs.z < 32;
    input->spectator = !available;
    input->notarget = input->animate_player = input->haste = input->no_stack_double = false;
    input->instant_switch = input->quick_switch = input->infinite_ammo = false;
    input->weapon_thunk = input->rune_damage = false;
    input->players_collide = true; input->hand = QA_Q2_RIGHT_HAND;
    input->quad_until_ns = input->double_until_ns = input->quad_fire_until_ns = 0;
    return true;
}
bool application_q3_weapons_services_q2_muzzle(void *context, const qa_builtin_event *event,
    qa_error *error)
{
    qa_application *app = context;
    if (!app || !event) return application_fail(error, QA_ERROR_ARGUMENT, "Source muzzle animation has no actual event");
    if (event->kind != QA_BUILTIN_MUZZLE || event->family != QA_GAME_Q2) return true;
    application_provider *provider = application_provider_for(app, event->actor, QA_ROLE_ARSENAL, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || provider->owner != event->provider)
        return true;
    application_provider *character = application_provider_for(app, event->actor, QA_ROLE_CHARACTER, "");
    if (character && character->kind == APPLICATION_PROVIDER_Q2) return true;
    /* The muzzle code identifies its real presentation event, not an item.
     * The donor's Q2 muzzle animation always requests the non-melee operation. */
    return application_q3_weapons_services_selected_fired(provider, event->actor, 0, error);
}
bool application_q3_weapons_services_pose(void *context, qa_actor_id actor,
    qa_q3_selected_source_pose *out, qa_error *error)
{
    q3g_role *role = context; application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    q3_weapon_actor source; qa_q3_player player; qa_q3_selected_source_pose pose = {0}; qa_combat_state combat;
    if (!out || !q3_weapon_services_source(s, actor, &source, error)) return false;
    ++s->calls;
    bool ok = qa_qvm_read_player(role->vm, (int32_t)source.player, true, &player, error) &&
        application_q3_weapons_max_health(role->weapons, actor, &pose.max_health, error) &&
        application_q3_weapons_powerup_until(role->weapons, actor, APPLICATION_Q3_QUAD, &pose.quad_until_ms, error) &&
        application_q3_weapons_powerup_until(role->weapons, actor, APPLICATION_Q3_HASTE, &pose.haste_until_ms, error) &&
        qa_combat_read(application(s)->combat, actor, &combat, error) && q3_weapons_current(role->weapons, &source);
    if (ok) {
        pose.view_angles = qa_v3(player.viewangles[0], player.viewangles[1], player.viewangles[2]);
        pose.view_height = (float)player.viewheight;
        pose.team = qa_team_color_number(qa_session_strings(application(s)->session), combat.team, QA_TEAM_COLOR_ALIASES);
        *out = pose;
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_damage(void *context, qa_actor_id actor,
    float *out, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; bool present;
    if (!out || !handled || !selected_original(context, actor, QA_Q3_ARSENAL, &s, NULL, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    ++s->calls;
    bool ok = application_q3_weapons_damage_factor(s->role->weapons, actor, out, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_original(context, actor, QA_Q3_ARSENAL, &after, NULL, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected damage replaced its original GAME owner");
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_spawn(void *context, qa_actor_id actor,
    qa_vec3 *origin, qa_vec3 *angles, bool *found, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; struct application_native_q2 *native; bool present;
    if (!origin || !angles || !found || !handled ||
        !selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &s, &native, &present, error)) return false;
    *handled = present; *found = false;
    if (!present) return true;
    if (native) return application_native_q2_player_spawn_point(native, actor, origin, angles, found, handled, error);
    ++s->calls;
    bool ok = application_q3_weapons_spawn_point(s->role->weapons, actor, origin, angles, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &after, NULL, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected spawn replaced its original GAME owner");
    }
    *found = ok; --s->calls; return ok;
}
bool application_q3_weapons_services_selected_drop(void *context, qa_actor_id actor,
    bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; struct application_native_q2 *native; bool present;
    if (!handled || !selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT,
            &s, &native, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    if (native) return application_native_q2_player_objectives_drop(native, actor, handled, error);
    ++s->calls;
    bool ok = application_q3_weapons_drop_objectives(s->role->weapons, actor, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &after, NULL, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected objective drop replaced its original GAME owner");
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_client_effects(void *context, qa_actor_id actor,
    const qa_q3_selected_client_effects *before, const qa_q3_selected_client_effects *after,
    bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; struct application_native_q2 *native; bool present;
    if (!before || !after || !handled ||
        !selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &s, &native, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    application_provider *provider = context;
    qa_application *app = provider->application;
    if (s) ++s->calls;
    bool ok = true, applied;
    if (before->max_health != after->max_health)
        ok = native ? application_native_q2_player_max_health_write(native, actor, after->max_health, &applied, error) :
            application_q3_weapons_set_max_health(s->role->weapons, actor, after->max_health, error);
    if (ok && before->teleport_bit != after->teleport_bit) {
        qa_body_state body;
        ok = qa_world_body_read(app->world, actor, &body, error);
        if (ok) ok = native ? application_native_q2_player_teleport(native, actor, body.origin, body.velocity,
                after->view_angles, after->pm_time_ms > 0 ? (uint32_t)after->pm_time_ms : 0, &applied, error) :
            application_q3_weapons_teleport_state(s->role->weapons, actor,
                body.origin, body.velocity, after->view_angles, after->pm_time_ms, error);
        if (ok) ok = qa_equipment_release_grapple(app->equipment, actor, error) &&
            qa_q3_release_grapple(provider->state.q3, actor, error);
    }
    application_q3_weapons_services *current; struct application_native_q2 *current_native; bool still;
    if (ok) {
        ok = selected_original(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &current, &current_native, &still, error);
        if (ok && (!still || current != s || current_native != native))
            ok = application_fail(error, QA_ERROR_NOT_FOUND,
                "Selected client effects replaced their original GAME owner");
    }
    if (s) --s->calls;
    return ok;
}

typedef struct selected_inventory {
    qa_arena storage;
    application_provider *provider;
    qa_inventory_source_snapshot definitions;
    qa_item_definition *weapons;
    size_t weapon_count;
    qa_inventory_entry *entries;
    size_t entry_count;
} selected_inventory;
static void inventory_free(selected_inventory *view)
{
    qa_arena_destroy(&view->storage); *view = (selected_inventory){0};
}
static bool selection_current(application_q3_weapons_services *s, qa_actor_id actor,
    application_provider *p, qa_error *error)
{
    application_provider *actual; bool present;
    return selected_arsenal(s, actor, &actual, &present, error) && present &&
        (actual == p || application_fail(error, QA_ERROR_NOT_FOUND, "Selected arsenal changed during its original Source action"));
}
static bool inventory_read(application_q3_weapons_services *s, qa_actor_id actor,
    selected_inventory *out, bool *present, qa_error *error)
{
    if (!selected_arsenal(s, actor, &out->provider, present, error)) return false;
    if (!*present) return true;
    qa_inventory *inventory = application(s)->inventory;
    qa_arena_init_pool(&out->storage, &s->inventory_pages);
    if (!qa_inventory_source_items(inventory, actor, &out->storage, &out->definitions, error)) return false;
    size_t weapons = 0;
    for (size_t i = 0; i < out->definitions.group_count; ++i)
        if (out->definitions.groups[i].owner == out->provider->owner) weapons += out->definitions.groups[i].count;
    out->weapons = weapons ? qa_arena_alloc(&out->storage, weapons * sizeof(*out->weapons),
        _Alignof(qa_item_definition), error) : NULL;
    if (weapons && !out->weapons) return false;
    for (size_t i = 0; i < out->definitions.group_count; ++i) {
        const qa_inventory_source_group *group = out->definitions.groups + i;
        if (group->owner != out->provider->owner) continue;
        for (size_t j = 0; j < group->count; ++j) {
            const qa_item_definition *item = &group->items[j].definition;
            if (!item->weapon) continue;
            out->weapons[out->weapon_count++] = *item;
        }
    }
    size_t count = 0;
    if (!out->weapon_count || !qa_inventory_entries(inventory, actor, NULL, 0, &count, error) ||
        count > SIZE_MAX / sizeof(*out->entries))
        return application_fail(error, QA_ERROR_FORMAT, "Selected arsenal has no bounded actual inventory declarations");
    out->entries = count ? qa_arena_alloc(&out->storage, count * sizeof(*out->entries), _Alignof(qa_inventory_entry), error) : NULL;
    if (count && !out->entries) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected Source grant entries");
    return qa_inventory_entries(inventory, actor, out->entries, count, &out->entry_count, error) &&
        selection_current(s, actor, out->provider, error);
}
static bool catalog_read(application_q3_weapons_services *s, qa_arena *storage,
    application_q3_weapon_catalog_entry **out, size_t *count, qa_error *error)
{
    if (!application_q3_catalog_current(s->role->catalog, s->image, s->vm, s->abi))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original Source grant lost its actual item catalog");
    const application_q3_catalog_weapon *borrowed;
    if (!application_q3_catalog_weapons(s->role->catalog, &borrowed, count, error)) return false;
    if (*count > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_MEMORY, "Actual Source catalog exceeds its retained roster extent");
    *out = *count ? qa_arena_alloc(storage, *count * sizeof(**out), _Alignof(application_q3_weapon_catalog_entry), error) : NULL;
    if (*count && !*out) return application_fail(error, QA_ERROR_MEMORY, "Retaining original catalog across inventory callbacks");
    for (size_t i = 0; i < *count; ++i)
        (*out)[i] = (application_q3_weapon_catalog_entry){borrowed[i].weapon, borrowed[i].item, borrowed[i].ammo};
    return application_q3_weapons_catalog_refresh(s->role->weapons, *out, *count, error);
}
static bool give_category(void *context, qa_actor_id actor, application_q3_weapon_grant category, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    ++s->calls;
    bool handled;
    bool ok = application_supplies_cheat_arsenal(s->role->engine->provider, actor,
        category == APPLICATION_Q3_GIVE_WEAPONS ? QA_Q1_CHEAT_WEAPONS : QA_Q1_CHEAT_AMMO,
        &handled, error);
    --s->calls; return ok;
}
static bool give_named(void *context, qa_actor_id actor, qa_bytes bytes, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!handled || (bytes.size && !bytes.data) || bytes.size == SIZE_MAX ||
        (bytes.size && memchr(bytes.data, 0, bytes.size)))
        return application_fail(error, QA_ERROR_FORMAT, "Selected grant name is not a counted Source string");
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    *handled = false;
    qa_unified_frame_lease *storage = application_control_storage_acquire(application(s), error);
    if (!storage) return false;
    char *name = qa_unified_frame_lease_alloc(storage, bytes.size + 1, 1, 1, error);
    bool ok = name != NULL;
    if (ok) {
        if (bytes.size) memcpy(name, bytes.data, bytes.size);
        name[bytes.size] = 0;
        const char *arguments[] = {name};
        ++s->calls;
        ok = application_supplies_cheat_item(s->role->engine->provider, actor, 1, arguments, handled, error);
        --s->calls;
    }
    qa_unified_frame_lease_release(storage); return ok;
}
static bool completed(void *context, qa_actor_id actor,const qa_qvm_call *call,
    bool reached, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    return application_guest_input_weapon_completed(s->role, actor, call, reached, error);
}
static bool prepare(void *context, qa_actor_id actor, const qa_qvm_call *call,
    qa_unified_frame_lease *storage, application_q3_weapon_preparation *out, qa_error *error)
{ application_q3_weapons_services *s = context; return application_guest_input_prepare_weapon(s->role, actor, call, storage, out, error); }

static bool drop_selected(void *context, qa_actor_id actor, application_q3_weapon_drop *out, qa_error *error)
{
    application_q3_weapons_services *s = context;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Selected death drop requires its projection result");
    *out = (application_q3_weapon_drop){0};
    ++s->calls;
    selected_inventory view = {0}; bool present = false;
    bool ok = inventory_read(s, actor, &view, &present, error);
    qa_item_id active = 0;
    if (ok && present) ok = qa_application_weapon_read(application(s), actor, &active, error) &&
        selection_current(s, actor, view.provider, error);
    if (ok && present) out->present = out->inventory = true;
    application_q3_weapon_catalog_entry *catalog = NULL; size_t count = 0;
    qa_supply_weapon *selected_roster = NULL, *original_roster = NULL;
    qa_item_id *sources = NULL;
    if (ok && present && active) ok = catalog_read(s, &view.storage, &catalog, &count, error);
    if (ok && present && active) {
        if (view.weapon_count > SIZE_MAX / sizeof(*selected_roster) || count > SIZE_MAX / sizeof(*original_roster))
            ok = application_fail(error, QA_ERROR_MEMORY, "Selected death-drop rosters exceed native extent");
        if (ok) {
            selected_roster = qa_arena_alloc(&view.storage, view.weapon_count * sizeof(*selected_roster), _Alignof(qa_supply_weapon), error);
            original_roster = count ? qa_arena_alloc(&view.storage, count * sizeof(*original_roster), _Alignof(qa_supply_weapon), error) : NULL;
            sources = qa_arena_alloc(&view.storage, view.weapon_count * sizeof(*sources), _Alignof(qa_item_id), error);
            if (!selected_roster || (count && !original_roster) || !sources)
                ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual selected death-drop roster relationships");
        }
        for (size_t i = 0; ok && i < view.weapon_count; ++i)
            selected_roster[i] = (qa_supply_weapon){view.weapons[i].item, view.weapons[i].ammo,
                application_supplies_weapon_droppable(view.provider, view.weapons[i].item)};
        for (size_t i = 0; ok && i < count; ++i)
            original_roster[i] = (qa_supply_weapon){catalog[i].item, catalog[i].ammo, true};
        if (ok) ok = application_supplies_weapon_sources(application(s)->supplies, s->role->engine->provider, actor,
            selected_roster, view.weapon_count, original_roster, count, &view.storage, sources, error) &&
            selection_current(s, actor, view.provider, error);
        size_t selected_index = view.weapon_count;
        for (size_t i = 0; ok && i < view.weapon_count; ++i) if (selected_roster[i].item == active) { selected_index = i; break; }
        if (ok && selected_index == view.weapon_count)
            ok = application_fail(error, QA_ERROR_FORMAT, "Selected active death-drop weapon lacks its actual declaration");
        if (ok && sources[selected_index]) {
            size_t original_index = count;
            for (size_t i = 0; i < count; ++i) if (catalog[i].item == sources[selected_index]) { original_index = i; break; }
            if (original_index == count) ok = application_fail(error, QA_ERROR_FORMAT, "Selected death drop lost its admitted original weapon");
            double ammo = 0;
            qa_item_id ammo_item = selected_roster[selected_index].ammo;
            if (ok && ammo_item) ok = qa_inventory_count_read(application(s)->inventory, actor, ammo_item, &ammo, error) &&
                selection_current(s, actor, view.provider, error);
            if (ok && (!isfinite(ammo) || ammo != trunc(ammo) || ammo < INT32_MIN || ammo > INT32_MAX))
                ok = application_fail(error, QA_ERROR_FORMAT, "Selected death-drop ammo is outside its original int32 counter");
            if (ok) {
                out->weapon = catalog[original_index].weapon; out->ammo = (int32_t)ammo;
                guest_inventory_projection_word projected[2]; size_t word_count = 0;
                if (catalog[original_index].ammo)
                    ok = application_guest_inventory_project(s->role, actor, catalog[original_index].ammo,
                        out->ammo, projected, &word_count, error) && selection_current(s, actor, view.provider, error);
                if (ok) {
                    for (size_t i = 0; i < word_count; ++i)
                        s->drop_words[i] = (qa_qvm_source_word){projected[i].address, projected[i].value};
                    out->words = s->drop_words; out->word_count = word_count;
                }
            }
        }
    }
    inventory_free(&view); --s->calls; return ok;
}
application_q3_weapon_services application_q3_weapons_services_callbacks(application_q3_weapons_services *s)
{
    return (application_q3_weapon_services){.context = s, .selected = selected,
        .attempted = attempted, .accepted = accepted, .completed = completed,
        .give = give_category, .give_item = give_named, .drop = drop_selected, .prepare_weapon = prepare};
}

bool application_q3_weapons_services_idle(const application_q3_weapons_services *s)
{ return !s || (!s->calls && !qa_qvm_active(s->vm)); }
bool application_q3_weapons_services_create(q3g_role *role, application_q3_weapons_services **out, qa_error *error)
{
    if (!role || !role->vm || !role->image || role->kind != QA_QVM_GAME || !out || *out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon services require a retained GAME constructor");
    application_q3_weapons_services *s = calloc(1, sizeof(*s));
    if (!s) return application_fail(error, QA_ERROR_MEMORY, "Retaining original weapon application services");
    s->role = role; s->image = role->image; s->vm = role->vm; s->abi = role->abi;
    const qa_actor_registry *actors = qa_session_actors(application(s)->session);
    size_t capacity = qa_actors_capacity(actors);
    if (capacity > SIZE_MAX / 2) {
        free(s);
        return application_fail(error, QA_ERROR_MEMORY, "Original weapon service storage exceeds native extent");
    }
    if (!qa_pool_prepare(&s->state_pool, &s->state_storage, capacity * 2,
            sizeof(q3_weapon_storage), _Alignof(q3_weapon_storage), error) ||
        !qa_actor_index_create(&s->actor_states, actors, &s->state_storage, error) ||
        !qa_id_index_reserve(&s->actor_states.retained, capacity, error)) {
        qa_actor_index_destroy(&s->actor_states);
        qa_arena_destroy(&s->state_storage); free(s); return false;
    }
    qa_strings *strings = qa_session_strings(application(s)->session);
    size_t items = qa_strings_count(strings), text_bytes = 0;
    if (items < 4096) items = 4096;
    for (qa_string_id i = 1; i <= qa_strings_count(strings); ++i) text_bytes += qa_strings_text(strings, i).size + 1;
    size_t bytes = items * (sizeof(qa_item_admission) + sizeof(qa_inventory_entry) +
        sizeof(qa_inventory_source_group) + sizeof(qa_item_definition) + sizeof(qa_supply_weapon) * 2) + text_bytes;
    size_t pages = (bytes * 4 + 4095) / 4096;
    if (!qa_pool_reserve_pages(&s->inventory_pages,&s->inventory_storage,pages,4096,_Alignof(max_align_t),error)) {
        qa_arena_destroy(&s->inventory_storage);
        qa_actor_index_destroy(&s->actor_states);
        qa_arena_destroy(&s->state_storage); free(s); return false;
    }
    *out = s; return true;
}
bool application_q3_weapons_services_destroy(application_q3_weapons_services **owner, qa_error *error)
{
    if (!owner || !*owner) return true;
    application_q3_weapons_services *s = *owner;
    if (!application_q3_weapons_services_idle(s))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon services retain an actual Source action");
    if (!application_q3_weapons_services_match_close(s, error)) return false;
    q3_weapon_requests_clear(s);
    qa_actor_index_destroy(&s->actor_states);
    qa_arena_destroy(&s->state_storage);
    qa_arena_destroy(&s->inventory_storage);
    free(s); *owner = NULL; return true;
}
