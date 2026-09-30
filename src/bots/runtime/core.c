#include "internal.h"
#include "../checkpoint_internal.h"

bool bot_runtime_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_runtime_mutable(qa_bot_runtime *r, qa_error *e) {
    return r && !r->closed && !r->busy && !r->observation_leases ? true :
        bot_runtime_fail(e, "bot runtime is absent, closed or executing a callback");
}
bool qa_bot_runtime_can_destroy(const qa_bot_runtime *r) {
    return !r || (!r->busy && !r->observation_leases && !r->owner_leases &&
        !qa_bot_moves_active(r->moves) && !qa_bot_goals_active(r->goals) &&
        !qa_bot_chat_system_active(r->chat_system));
}
bool bot_runtime_owners_idle(qa_bot_runtime *r, qa_error *e) {
    return r && qa_bot_runtime_can_destroy(r) ? true :
        bot_runtime_fail(e, "bot runtime owners are absent or in use");
}
bool qa_bot_runtime_lease_begin(qa_bot_runtime *r, qa_error *e) {
    if (!r || r->owner_leases == SIZE_MAX)
        return bot_runtime_fail(e, "bot runtime owner lease is unavailable");
    ++r->owner_leases;
    return true;
}
void qa_bot_runtime_lease_end(qa_bot_runtime *r) {
    if (r && r->owner_leases) --r->owner_leases;
}
bool bot_runtime_restore_begin(qa_bot_runtime *r, qa_error *e) {
    if (!bot_runtime_mutable(r,e) || !r->goals || !r->moves || !r->actions ||
        qa_bot_goals_active(r->goals) || qa_bot_moves_active(r->moves) ||
        qa_bot_chat_system_active(r->chat_system))
        return bot_runtime_fail(e,"bot checkpoint owners are unavailable or active");
    r->busy=true;
    bot_goal_restore_lock(r->goals,true);
    bot_move_restore_lock(r->moves,true);
    bot_action_restore_lock(r->actions,true);
    return true;
}
void bot_runtime_restore_end(qa_bot_runtime *r) {
    bot_action_restore_lock(r->actions,false);
    bot_move_restore_lock(r->moves,false);
    bot_goal_restore_lock(r->goals,false);
    r->busy=false;
}
bool bot_runtime_variable(qa_bot_runtime *r, const char *name, const char *fallback,
                           const qa_bot_variable **out, qa_error *e) {
    return qa_bot_library_variable_default(r->library, name, fallback, out, e);
}
bool bot_runtime_integer(qa_bot_runtime *r, const char *name, const char *fallback,
                          int32_t *out, qa_error *e) {
    const qa_bot_variable *v;
    if (!bot_runtime_variable(r, name, fallback, &v, e)) return false;
    if (!isfinite(v->value) || v->value < -2147483648.0f || v->value >= 2147483648.0f)
        return bot_runtime_fail(e, "bot library variable exceeds source integer range");
    *out = (int32_t)v->value;
    return true;
}
bool qa_bot_runtime_create(const qa_bot_runtime_options *options,
                            const qa_bot_runtime_services *services, qa_bot_runtime **out, qa_error *e) {
    if (!options || !services || !out || !services->random.next || !services->navigation ||
        (!!services->goals.pickups != !!services->goals.pickups_end) ||
        (services->goals.pickups && !services->goals.pickup) ||
        options->observations > QA_BOT_OBSERVATION_MODULE)
        return bot_runtime_fail(e, "invalid bot runtime services/profile");
    uint32_t maximum = options->maximum_states ? options->maximum_states : 64;
    if (maximum > INT32_MAX || maximum > SIZE_MAX / sizeof(bot_weapon_state))
        return bot_runtime_fail(e, "bot runtime handle capacity overflow");
    qa_bot_runtime *r = calloc(1, sizeof(*r));
    if (!r) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot runtime"); return false; }
    r->options = *options;
    r->options.maximum_states = maximum;
    r->services = *services;
    r->characters = calloc(maximum, sizeof(*r->characters));
    r->weapons = calloc(maximum, sizeof(*r->weapons));
    r->chats = calloc(maximum, sizeof(*r->chats));
    if (!r->characters || !r->weapons || !r->chats) {
        qa_error_set(e, QA_ERROR_MEMORY, maximum, "allocating bot runtime handle tables");
        (void)qa_bot_runtime_destroy(r, NULL);
        return false;
    }
    if (!qa_bot_library_create(&options->library, &r->library, e) ||
        !bot_runtime_owners_create(r, e)) {
        (void)qa_bot_runtime_destroy(r, NULL);
        return false;
    }
    *out = r;
    return true;
}
static void close(qa_bot_runtime *r) {
    bot_runtime_handles_close(r);
    qa_bot_moves_destroy(r->moves); r->moves = NULL;
    qa_bot_goals_destroy(r->goals); r->goals = NULL;
    qa_bot_chat_system_destroy(r->chat_system); r->chat_system = NULL;
    qa_bot_weapons_release(r->weapon_config); r->weapon_config = NULL;
    qa_bot_actions_shutdown(r->actions);
    qa_bot_library_destroy(r->library); r->library = NULL;
    if (r->options.observations != QA_BOT_OBSERVATION_MODULE) {
        bot_runtime_observations_close(r);
        qa_bot_bsp_close(r->bsp); r->bsp = NULL;
        r->bsp_loaded = false;
        free(r->map_name); r->map_name = NULL;
        r->map = (qa_bot_runtime_map){0};
    }
    r->initialized = r->library_initialized = r->loaded = false;
    r->closed = true;
}
bool qa_bot_runtime_destroy(qa_bot_runtime *r, qa_error *e) {
    if (!r) return true;
    if (!bot_runtime_owners_idle(r, e)) return false;
    close(r);
    qa_bot_actions_destroy(r->actions);
    bot_runtime_observations_close(r);
    qa_bot_bsp_close(r->bsp);
    free(r->map_name);
    free(r->characters); free(r->weapons); free(r->chats);
    free(r);
    return true;
}
bool qa_bot_runtime_shutdown(qa_bot_runtime *r, qa_error *e) {
    if (r && r->closed) return true;
    if (!bot_runtime_mutable(r, e)) return false;
    if (!bot_runtime_owners_idle(r, e)) return false;
    close(r);
    return true;
}
bool qa_bot_runtime_initialized(const qa_bot_runtime *r) { return r && r->initialized && !r->closed; }
bool qa_bot_runtime_loaded(const qa_bot_runtime *r) { return r && r->loaded && !r->closed; }
bool qa_bot_runtime_closed(const qa_bot_runtime *r) { return r && r->closed; }
float qa_bot_runtime_time(const qa_bot_runtime *r) { return r ? r->time : 0; }
bool qa_bot_runtime_debug(const qa_bot_runtime *r) { return r && r->options.debug; }
qa_bot_random_source qa_bot_runtime_random_source(const qa_bot_runtime *r) {
    return r ? r->services.random : (qa_bot_random_source){0};
}
qa_bot_library *qa_bot_runtime_library(qa_bot_runtime *r) { return r ? r->library : NULL; }
qa_bot_actions *qa_bot_runtime_actions(qa_bot_runtime *r) { return r ? r->actions : NULL; }
qa_bot_goals *qa_bot_runtime_goals(qa_bot_runtime *r) { return r ? r->goals : NULL; }
qa_bot_moves *qa_bot_runtime_moves(qa_bot_runtime *r) { return r ? r->moves : NULL; }
qa_bot_chat_system *qa_bot_runtime_chat_system(qa_bot_runtime *r) { return r ? r->chat_system : NULL; }
qa_bot_navigation *qa_bot_runtime_navigation(qa_bot_runtime *r, int32_t client) {
    if (!r) return NULL;
    if (client < 0 && r->map.navigation) return r->map.navigation;
    bool previous = r->busy;
    r->busy = true;
    qa_bot_navigation *navigation = r->services.navigation(r->services.context, client);
    r->busy = previous;
    return navigation;
}
bool qa_bot_runtime_predict_movement(qa_bot_runtime *r, int32_t client,
                                      const qa_bot_movement_prediction_query *query,
                                      qa_bot_movement_prediction *out, qa_error *e) {
    if (!r || r->busy || r->observation_leases ||
        (r->closed && r->options.observations != QA_BOT_OBSERVATION_MODULE))
        return bot_runtime_fail(e, "bot runtime is absent or executing a callback");
    r->busy = true;
    qa_bot_navigation *navigation = r->services.navigation(r->services.context, client);
    bool ok = navigation && qa_bot_navigation_actor(navigation).registry ?
        qa_bot_navigation_predict_movement(navigation, query, out, e) :
        bot_runtime_fail(e, "source movement prediction client is not admitted");
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_attach_map(qa_bot_runtime *r, const qa_bot_runtime_map *map, qa_error *e) {
    if (!bot_runtime_mutable(r, e) || !bot_runtime_owners_idle(r, e)) return false;
    if (!map || !map->name || !map->navigation || (map->source_entities.size && !map->source_entities.data))
        return bot_runtime_fail(e, "invalid bot map binding");
    size_t size = strlen(map->name) + 1;
    char *name = malloc(size);
    if (!name) { qa_error_set(e, QA_ERROR_MEMORY, size, "retaining bot map identity"); return false; }
    memcpy(name, map->name, size);
    free(r->map_name);
    r->map_name = name;
    r->map = *map;
    r->map.name = name;
    r->loaded = false;
    return true;
}
bool qa_bot_runtime_load_map(qa_bot_runtime *r, const char *name, qa_error *e) {
    if (!bot_runtime_owners_idle(r, e)) return false;
    bool module = r->options.observations == QA_BOT_OBSERVATION_MODULE;
    if ((!module && (!r->initialized || r->closed)) ||
        !name || !r->map_name || strcmp(name, r->map_name) != 0)
        return bot_runtime_fail(e, "bot map name does not match the attached canonical world");
    qa_bot_bsp *bsp = NULL;
    r->busy = true;
    bool ok = true;
    if (r->map.source_entities.data || !r->map.entities) {
        qa_script_lexer_options options = {.token_limit = r->options.library.preprocessor.token_limit,
            .context = r->options.library.scripts.context,
            .diagnostic = r->options.library.scripts.diagnostic};
        ok = qa_bot_bsp_load(r->map.source_entities, &options, &bsp, e);
    }
    const qa_entities *entities = bsp ? qa_bot_bsp_entities(bsp) : r->map.entities;
    if (ok && module) {
        qa_bot_bsp_close(r->bsp);
        r->bsp = bsp;
        bsp = NULL;
        r->bsp_loaded = true;
        bot_runtime_observations_clear(r);
        if (r->closed || !r->library_initialized)
            ok = bot_runtime_fail(e, "bot library is closed or not initialized");
    }
    if (ok) ok = qa_bot_goals_load_map(r->goals, entities, r->map.navigation, e);
    if (ok) {
        if (!module) {
            qa_bot_bsp_close(r->bsp);
            r->bsp = bsp;
            r->bsp_loaded = true;
            bot_runtime_observations_clear(r);
        }
        r->loaded = true;
    } else qa_bot_bsp_close(bsp);
    r->busy = false;
    return ok;
}
const qa_entities *qa_bot_runtime_bsp(const qa_bot_runtime *r) {
    return r && r->bsp_loaded ?
        r->bsp ? qa_bot_bsp_entities(r->bsp) : r->map.entities : NULL;
}
bool qa_bot_runtime_init_level_items(qa_bot_runtime *r, qa_error *e) {
    if (!bot_runtime_mutable(r, e) || !bot_runtime_owners_idle(r, e)) return false;
    const qa_entities *entities = qa_bot_runtime_bsp(r);
    if (!entities || !r->map.navigation)
        return bot_runtime_fail(e, "BotInitLevelItems requires a retained goal world");
    r->busy = true;
    bool ok = qa_bot_goals_load_map(r->goals, entities, r->map.navigation, e);
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_start_frame(qa_bot_runtime *r, float time, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!r->library_initialized || !isfinite(time)) return bot_runtime_fail(e, "invalid bot source frame");
    if (!qa_bot_goals_time(r->goals, time, e) || !qa_bot_moves_time(r->moves, time, e)) return false;
    r->time = time;
    return true;
}
