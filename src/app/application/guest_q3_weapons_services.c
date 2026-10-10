#include "guest_q3_weapons_services_private.h"
#include "guest_q3_catalog.h"
#include "guest_projection_private.h"
#include "guest_input_private.h"
#include "supplies.h"
#include "equipment_runtime.h"
#include <ctype.h>

static qa_application *application(application_q3_weapons_services *s)
{ return s->role->engine->provider->application; }
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
    if (ok) *out = state.team;
    --s->calls; return ok;
}
static bool match_set_team(void *context, qa_team_id team, qa_error *error)
{
    q3_weapon_match *match = context; application_q3_weapons_services *s = match->services;
    q3_weapon_actor source; qa_team_id current;
    if (s->closing_match)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original match team mutation lost its current owner");
    if (!match_current(match, &source, error) || !match_team(match, &current, error)) return false;
    if (current == team) return true;
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
    q3_weapon_match *match = s->matches;
    while (match && (!qa_actor_id_equal(match->actor, actor) || match->mode.slot != mode.slot ||
        match->mode.generation != mode.generation)) match = match->next;
    if (!match) {
        match = calloc(1, sizeof(*match));
        if (!match) return application_fail(error, QA_ERROR_MEMORY, "Retaining genuine original match callback owner");
        *match = (q3_weapon_match){.next = s->matches, .services = s, .actor = actor,
            .source_owner = owner, .mode = mode}; s->matches = match;
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
        s->matches = match->next; free(match);
    }
    s->closing_match = false; return true;
}
static bool native_selected(application_q3_weapons_services *s, qa_actor_id actor,
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
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player; *present = qa_q1_player_read(p->state.q1, actor, &player);
    } else if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        if (!qa_q2_weapon_read(p->state.q2, actor, &player, error)) return false;
        *present = true;
    } else if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        *present = qa_q3_player_read(p->state.q3, actor, &player) && (player.selections & QA_Q3_ARSENAL);
    }
    return *present || application_fail(error, QA_ERROR_NOT_FOUND, "Selected original weapon services have no admitted native arsenal actor");
}
static bool selected(void *context, qa_actor_id actor)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, NULL)) return false;
    qa_application *app = application(s);
    return application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == s->role->engine->provider &&
        (!app->equipment || qa_equipment_primary_selected(app->equipment, actor));
}
static size_t request_index(const application_q3_weapons_services *s, qa_actor_id actor)
{
    for (size_t i = 0; i < s->request_count; ++i)
        if (qa_actor_id_equal(s->requests[i].actor, actor)) return i;
    return s->request_count;
}
static void request_remove(application_q3_weapons_services *s, size_t i)
{
    if (i == s->request_count) return;
    memmove(s->requests + i, s->requests + i + 1, (s->request_count - i - 1) * sizeof(*s->requests));
    --s->request_count;
}
static bool attempted(void *context, qa_actor_id actor, int32_t weapon, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    size_t i = request_index(s, actor);
    if (i < s->request_count && s->requests[i].weapon != weapon) request_remove(s, i);
    return true;
}
static bool request_store(application_q3_weapons_services *s, qa_actor_id actor,
    int32_t weapon, qa_error *error)
{
    size_t i = request_index(s, actor);
    if (i == s->request_count) {
        if (s->request_count == SIZE_MAX / sizeof(*s->requests))
            return application_fail(error, QA_ERROR_MEMORY, "Original weapon request continuation exceeds native extent");
        q3_weapon_request *grown = realloc(s->requests, (s->request_count + 1) * sizeof(*grown));
        if (!grown) return application_fail(error, QA_ERROR_MEMORY, "Retaining original weapon command request");
        s->requests = grown; ++s->request_count;
    }
    s->requests[i] = (q3_weapon_request){actor, weapon}; return true;
}
static bool accepted(void *context, qa_actor_id actor, int32_t weapon, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (weapon < 0) return application_fail(error, QA_ERROR_FORMAT, "Accepted Source weapon selection is negative");
    return q3_weapon_services_source(s, actor, &source, error) && request_store(s, actor, weapon, error);
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
        if (active != item) ok = request_store(s, actor, weapon, error);
        if (ok) *admitted = true;
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_request(application_q3_weapons_services *s, qa_actor_id actor,
    int32_t *out, bool *present, qa_error *error)
{
    q3_weapon_actor source;
    if (!out || !present || !q3_weapon_services_source(s, actor, &source, error)) return false;
    size_t i = request_index(s, actor); *present = i < s->request_count;
    if (*present) *out = s->requests[i].weapon;
    return true;
}
bool application_q3_weapons_services_request_completed(application_q3_weapons_services *s,
    qa_actor_id actor, qa_error *error)
{
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    size_t i = request_index(s, actor);
    if (i == s->request_count) return true;
    int32_t selection;
    if (!q3_weapons_read(s->role->weapons, source.player +
        s->role->weapons->profile.selection_offset, &selection, error)) return false;
    if (selection == s->requests[i].weapon) request_remove(s, i);
    return true;
}
void application_q3_weapons_services_actor_released(application_q3_weapons_services *s, qa_actor_record actor)
{ if (s) request_remove(s, request_index(s, actor.id)); }
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
static bool selected_qvm(void *context, qa_actor_id actor, uint32_t selection,
    application_q3_weapons_services **out, bool *present, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->application ||
        !provider->constructed || !provider->attached || provider->close_pending || !provider->state.q3 || !out || !present)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 effect lost its real native provider");
    *out = NULL; *present = false;
    qa_application *app = provider->application;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    qa_q3_player_state player;
    if (!qa_q3_player_read(provider->state.q3, actor, &player) ||
        !(((selection & player.selections & QA_Q3_ARSENAL) &&
            application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider) ||
          ((selection & player.selections & QA_Q3_EQUIPMENT) &&
            application_provider_for(app, actor, QA_ROLE_EQUIPMENT, "") == provider)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 effect differs from its actual actor role");
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    *out = s; *present = true; return true;
}
static bool team_name(const char *actual, const char *name)
{
    if (!actual) return false;
    while (*actual && *name) {
        if (tolower((unsigned char)*actual) != (unsigned char)*name) return false;
        ++actual; ++name;
    }
    return !*actual && !*name;
}
bool application_q3_weapons_services_grenade_interval(void *context, qa_actor_id actor,
    qa_actor_owner owner, uint64_t native, uint64_t *out, bool *handled, qa_error *error)
{
    qa_application *app = context;
    if (!app || !app->session || !out || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hand grenade cadence has no actual application");
    *handled = false;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
    q3_weapon_actor source; qa_equipment_state equipment;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &equipment) ||
        !equipment.selection.grenades.enabled || equipment.sources.grenades != owner ||
        !application_equipment_runtime_owner_current(app->equipment_runtime, owner))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its actual selected equipment");
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner) provider = app->providers[i];
    qa_q2_hand_grenade_state grenade; bool bound = false;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->state.q2)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its native provider");
    qa_q2_game *game = provider->state.q2;
    if (!qa_q2_hand_grenade_read(game, actor, &grenade, &bound, error)) return false;
    if (!bound)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Hand grenade cadence lost its native action owner");
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
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    qa_equipment_state equipment;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &equipment) ||
        equipment.selection.binding != QA_EQUIPMENT_WEAPON_SLOT ||
        equipment.sources.grapple != provider->owner ||
        !(equipment.slot_active || equipment.slot_lowering) ||
        !qa_equipment_weapon_presented(app->equipment, actor, provider->owner)) return true;
    if (!application_equipment_runtime_owner_current(app->equipment_runtime, provider->owner))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Equipment animation retired its retained source");
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
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
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM) return true;
    if (application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    if (app->equipment && !qa_equipment_primary_selected(app->equipment, actor)) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
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
    if (!physical || physical->kind != APPLICATION_PROVIDER_QVM ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider) return true;
    struct application_q3_guest *engine = q3g_engine(physical);
    q3g_role *role = engine ? engine->game : NULL;
    application_q3_weapons_services *s = role ? role->weapon_services : NULL;
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
        const char *team = qa_strings_cstr(qa_session_strings(application(s)->session), combat.team);
        pose.team = team_name(team, "team:red") || team_name(team, "red") || team_name(team, "5") ||
            team_name(team, "q3:1") || team_name(team, "q2:1") ? 1 :
            team_name(team, "team:blue") || team_name(team, "blue") || team_name(team, "14") ||
            team_name(team, "q3:2") || team_name(team, "q2:2") ? 2 : 0;
        *out = pose;
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_damage(void *context, qa_actor_id actor,
    float *out, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; bool present;
    if (!out || !handled || !selected_qvm(context, actor, QA_Q3_ARSENAL, &s, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    ++s->calls;
    bool ok = application_q3_weapons_damage_factor(s->role->weapons, actor, out, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_qvm(context, actor, QA_Q3_ARSENAL, &after, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected damage replaced its original GAME owner");
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_spawn(void *context, qa_actor_id actor,
    qa_vec3 *origin, qa_vec3 *angles, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; bool present;
    if (!origin || !angles || !handled ||
        !selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &s, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    ++s->calls;
    bool ok = application_q3_weapons_spawn_point(s->role->weapons, actor, origin, angles, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &after, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected spawn replaced its original GAME owner");
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_drop(void *context, qa_actor_id actor,
    bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; bool present;
    if (!handled || !selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &s, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    ++s->calls;
    bool ok = application_q3_weapons_drop_objectives(s->role->weapons, actor, error);
    application_q3_weapons_services *after; bool still;
    if (ok) {
        ok = selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &after, &still, error);
        if (ok && (!still || after != s)) ok = application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected objective drop replaced its original GAME owner");
    }
    --s->calls; return ok;
}
bool application_q3_weapons_services_selected_client_effects(void *context, qa_actor_id actor,
    const qa_q3_selected_client_effects *before, const qa_q3_selected_client_effects *after,
    bool *handled, qa_error *error)
{
    application_q3_weapons_services *s; bool present;
    if (!before || !after || !handled ||
        !selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &s, &present, error)) return false;
    *handled = present;
    if (!present) return true;
    ++s->calls;
    bool ok = before->max_health == after->max_health ||
        application_q3_weapons_set_max_health(s->role->weapons, actor, after->max_health, error);
    if (ok && before->teleport_bit != after->teleport_bit) {
        qa_application *app = application(s); qa_body_state body;
        application_provider *provider = context;
        ok = qa_world_body_read(app->world, actor, &body, error) &&
            application_q3_weapons_teleport_state(s->role->weapons, actor,
                body.origin, body.velocity, after->view_angles, after->pm_time_ms, error) &&
            qa_equipment_release_grapple(app->equipment, actor, error) &&
            qa_q3_release_grapple(provider->state.q3, actor, error);
    }
    application_q3_weapons_services *current; bool still;
    if (ok) {
        ok = selected_qvm(context, actor, QA_Q3_ARSENAL | QA_Q3_EQUIPMENT, &current, &still, error);
        if (ok && (!still || current != s))
            ok = application_fail(error, QA_ERROR_NOT_FOUND,
                "Selected client effects replaced their original GAME owner");
    }
    --s->calls; return ok;
}

typedef struct selected_inventory {
    application_provider *provider;
    qa_inventory_source_snapshot definitions;
    qa_item_definition *weapons;
    size_t weapon_count;
    qa_inventory_entry *entries;
    size_t entry_count;
} selected_inventory;
static void inventory_free(selected_inventory *view)
{
    qa_inventory_source_snapshot_free(&view->definitions);
    free(view->weapons); free(view->entries); *view = (selected_inventory){0};
}
static bool selection_current(application_q3_weapons_services *s, qa_actor_id actor,
    application_provider *p, qa_error *error)
{
    application_provider *actual; bool present;
    return native_selected(s, actor, &actual, &present, error) && present &&
        (actual == p || application_fail(error, QA_ERROR_NOT_FOUND, "Selected arsenal changed during its original Source action"));
}
static bool inventory_read(application_q3_weapons_services *s, qa_actor_id actor,
    selected_inventory *out, bool *present, qa_error *error)
{
    if (!native_selected(s, actor, &out->provider, present, error)) return false;
    if (!*present) return true;
    qa_inventory *inventory = application(s)->inventory;
    if (!qa_inventory_source_items(inventory, actor, &out->definitions, error)) return false;
    for (size_t i = 0; i < out->definitions.group_count; ++i) {
        const qa_inventory_source_group *group = out->definitions.groups + i;
        if (group->owner != out->provider->owner) continue;
        for (size_t j = 0; j < group->count; ++j) {
            const qa_item_definition *item = &group->items[j].definition;
            if (!item->weapon) continue;
            if (out->weapon_count == SIZE_MAX / sizeof(*out->weapons))
                return application_fail(error, QA_ERROR_MEMORY, "Selected weapon declarations exceed native extent");
            qa_item_definition *grown = realloc(out->weapons, (out->weapon_count + 1) * sizeof(*grown));
            if (!grown) return application_fail(error, QA_ERROR_MEMORY, "Reading actual selected weapon declarations");
            out->weapons = grown; grown[out->weapon_count++] = *item;
        }
    }
    size_t count = 0;
    if (!out->weapon_count || !qa_inventory_entries(inventory, actor, NULL, 0, &count, error) ||
        count > SIZE_MAX / sizeof(*out->entries))
        return application_fail(error, QA_ERROR_FORMAT, "Selected arsenal has no bounded actual inventory declarations");
    out->entries = count ? malloc(count * sizeof(*out->entries)) : NULL;
    if (count && !out->entries) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected Source grant entries");
    return qa_inventory_entries(inventory, actor, out->entries, count, &out->entry_count, error) &&
        selection_current(s, actor, out->provider, error);
}
static bool weapon_item(const selected_inventory *view, qa_item_id item)
{
    for (size_t i = 0; i < view->weapon_count; ++i) if (view->weapons[i].item == item) return true;
    return false;
}
static bool arsenal_item(const selected_inventory *view, qa_item_id item)
{
    for (size_t i = 0; i < view->weapon_count; ++i)
        if (view->weapons[i].item == item || view->weapons[i].ammo == item) return true;
    return false;
}
static bool catalog_read(application_q3_weapons_services *s,
    application_q3_weapon_catalog_entry **out, size_t *count, qa_error *error)
{
    if (!application_q3_catalog_current(s->role->catalog, s->image, s->vm, s->abi))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original Source grant lost its actual item catalog");
    const application_q3_catalog_weapon *borrowed;
    if (!application_q3_catalog_weapons(s->role->catalog, &borrowed, count, error)) return false;
    if (*count > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_MEMORY, "Actual Source catalog exceeds its retained roster extent");
    *out = *count ? malloc(*count * sizeof(**out)) : NULL;
    if (*count && !*out) return application_fail(error, QA_ERROR_MEMORY, "Retaining original catalog across inventory callbacks");
    for (size_t i = 0; i < *count; ++i)
        (*out)[i] = (application_q3_weapon_catalog_entry){borrowed[i].weapon, borrowed[i].item, borrowed[i].ammo};
    return application_q3_weapons_catalog_refresh(s->role->weapons, *out, *count, error);
}
static bool give_category(void *context, qa_actor_id actor, application_q3_weapon_grant category, qa_error *error)
{
    application_q3_weapons_services *s = context; selected_inventory view = {0}; bool present = false;
    ++s->calls;
    bool ok = inventory_read(s, actor, &view, &present, error);
    application_q3_weapon_catalog_entry *catalog = NULL; size_t count = 0;
    if (ok && present) ok = catalog_read(s, &catalog, &count, error);
    for (size_t i = 0; ok && present && i < view.entry_count; ++i) {
        qa_inventory_entry entry = view.entries[i]; bool original = false;
        if (!arsenal_item(&view, entry.item) || ((category == APPLICATION_Q3_GIVE_WEAPONS) != weapon_item(&view, entry.item))) continue;
        for (size_t j = 0; j < count; ++j)
            if (entry.item == catalog[j].item || entry.item == catalog[j].ammo) { original = true; break; }
        if (original) continue;
        entry.count = category == APPLICATION_Q3_GIVE_WEAPONS ? 1 : entry.capacity;
        ok = qa_inventory_configure(application(s)->inventory, actor, &entry, NULL, NULL, error) &&
            selection_current(s, actor, view.provider, error);
    }
    free(catalog); inventory_free(&view); --s->calls; return ok;
}
static bool normalized(const char *name, const char *requested)
{
    while (true) {
        while (*name == ' ' || *name == '_') ++name;
        while (*requested == ' ' || *requested == '_') ++requested;
        if (tolower((unsigned char)*name) != tolower((unsigned char)*requested)) return false;
        if (!*name) return true;
        ++name; ++requested;
    }
}
static bool give_named(void *context, qa_actor_id actor, qa_bytes bytes, bool *handled, qa_error *error)
{
    application_q3_weapons_services *s = context;
    if (!handled || (bytes.size && !bytes.data) || bytes.size == SIZE_MAX ||
        (bytes.size && memchr(bytes.data, 0, bytes.size)))
        return application_fail(error, QA_ERROR_FORMAT, "Selected grant name is not a counted Source string");
    *handled = false;
    char *name = malloc(bytes.size + 1);
    if (!name) return application_fail(error, QA_ERROR_MEMORY, "Retaining original named weapon grant");
    if (bytes.size) memcpy(name, bytes.data, bytes.size);
    name[bytes.size] = 0;
    ++s->calls;
    selected_inventory view = {0}; bool present = false;
    application_provider *arsenal = NULL;
    bool ok = native_selected(s, actor, &arsenal, &present, error);
    if (ok && present && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        ok = qa_q3_selected_holdable_give(arsenal->state.q3, actor, name, handled, error) &&
            selection_current(s, actor, arsenal, error);
        if (!ok || *handled) { --s->calls; free(name); return ok; }
    }
    if (ok) ok = inventory_read(s, actor, &view, &present, error);
    /* QVM passes its original ArgConcat string as one argument, so quantity
     * is absent here, matching the donor's giveSelectedItem(actor, [name]). */
    qa_item_id named = 0;
    for (size_t i = 0; ok && present && !named && i < view.weapon_count; ++i) {
        const char *identity = qa_strings_cstr(qa_session_strings(application(s)->session), view.weapons[i].item);
        if ((identity && normalized(identity, name)) || (view.weapons[i].label && normalized(view.weapons[i].label, name))) named = view.weapons[i].item;
    }
    for (size_t i = 0; ok && present && i < view.entry_count; ++i) {
        qa_inventory_entry entry = view.entries[i];
        if (!arsenal_item(&view, entry.item)) continue;
        const char *identity = qa_strings_cstr(qa_session_strings(application(s)->session), entry.item);
        const char *base = identity ? strrchr(identity, '/') : NULL;
        if (entry.item != named && (!identity || (!normalized(identity, name) && !normalized(base ? base + 1 : identity, name)))) continue;
        entry.count = weapon_item(&view, entry.item) ? 1 : entry.capacity;
        ok = qa_inventory_configure(application(s)->inventory, actor, &entry, NULL, NULL, error) &&
            selection_current(s, actor, view.provider, error);
        if (ok) *handled = true;
        break;
    }
    inventory_free(&view); --s->calls; free(name); return ok;
}
static bool completed(void *context, qa_actor_id actor, bool reached, qa_error *error)
{
    application_q3_weapons_services *s = context; q3_weapon_actor source;
    if (!q3_weapon_services_source(s, actor, &source, error)) return false;
    return application_guest_input_weapon_completed(s->role, actor, reached, error);
}
static bool prepare(void *context, qa_actor_id actor, const qa_qvm_call *call,
    application_q3_weapon_preparation *out, qa_error *error)
{ application_q3_weapons_services *s = context; return application_guest_input_prepare_weapon(s->role, actor, call, out, error); }

static bool can_drop(const selected_inventory *view, qa_item_id item,
    qa_strings *strings)
{
    if (view->provider->kind == APPLICATION_PROVIDER_Q3)
        return item != qa_q3_weapon_item(view->provider->state.q3, QA_Q3_W_GAUNTLET, false) &&
            item != qa_q3_weapon_item(view->provider->state.q3, QA_Q3_W_GRAPPLE, false);
    if (view->provider->kind == APPLICATION_PROVIDER_Q1) {
        const char *identity = qa_strings_cstr(strings, item);
        return identity && strcmp(identity, "q1:weapon/rogue:grapple") && strcmp(identity, "q1:weapon/ctf:grapple");
    }
    return true;
}
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
    if (ok && present && active) ok = catalog_read(s, &catalog, &count, error);
    if (ok && present && active) {
        if (view.weapon_count > SIZE_MAX / sizeof(*selected_roster) || count > SIZE_MAX / sizeof(*original_roster))
            ok = application_fail(error, QA_ERROR_MEMORY, "Selected death-drop rosters exceed native extent");
        if (ok) {
            selected_roster = calloc(view.weapon_count, sizeof(*selected_roster));
            original_roster = count ? calloc(count, sizeof(*original_roster)) : NULL;
            sources = calloc(view.weapon_count, sizeof(*sources));
            if (!selected_roster || (count && !original_roster) || !sources)
                ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual selected death-drop roster relationships");
        }
        for (size_t i = 0; ok && i < view.weapon_count; ++i)
            selected_roster[i] = (qa_supply_weapon){view.weapons[i].item, view.weapons[i].ammo,
                can_drop(&view, view.weapons[i].item, qa_session_strings(application(s)->session))};
        for (size_t i = 0; ok && i < count; ++i)
            original_roster[i] = (qa_supply_weapon){catalog[i].item, catalog[i].ammo, true};
        if (ok) ok = application_supplies_weapon_sources(application(s)->supplies, s->role->engine->provider, actor,
            selected_roster, view.weapon_count, original_roster, count, sources, error) &&
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
    free(catalog); free(selected_roster); free(original_roster); free(sources);
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
    s->role = role; s->image = role->image; s->vm = role->vm; s->abi = role->abi; *out = s; return true;
}
bool application_q3_weapons_services_destroy(application_q3_weapons_services **owner, qa_error *error)
{
    if (!owner || !*owner) return true;
    application_q3_weapons_services *s = *owner;
    if (!application_q3_weapons_services_idle(s))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon services retain an actual Source action");
    if (!application_q3_weapons_services_match_close(s, error)) return false;
    free(s->requests); free(s); *owner = NULL; return true;
}
