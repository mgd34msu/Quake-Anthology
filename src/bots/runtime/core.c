#include "internal.h"
#include "../library/internal.h"
#include "../checkpoint_internal.h"
#include "source_weapon_setup.h"
#include "../library/internal.h"

bool bot_runtime_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_runtime_mutable(qa_bot_runtime *r, qa_error *e) {
    return r && !r->closed && !r->busy && !r->restore_pending && !r->observation_leases ? true :
        bot_runtime_fail(e, "bot runtime is absent, closed or executing a callback");
}
bool qa_bot_runtime_can_destroy(const qa_bot_runtime *r) {
    return !r || (!r->busy && !r->observation_leases && !r->owner_leases && qa_bot_log_can_destroy(r->log) &&
        qa_bot_memory_idle(r->memory) && qa_bot_library_idle(r->library) &&
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
static bool log_open(void *context,const char *name,bool resume,uint64_t position,
    qa_bot_log_stream *out,qa_error *error) {
    qa_bot_runtime *runtime=context;
    return runtime->services.log.open?runtime->services.log.open(runtime->services.log.context,name,resume,position,out,error):
        bot_runtime_fail(error,"Bot log has no actual file host");
}
static bool log_print(void *context,qa_script_severity severity,const char *text,qa_error *error) {
    qa_bot_runtime *runtime=context;
    if(runtime->services.log.print)
        return runtime->services.log.print(runtime->services.log.context,severity,text,error);
    if(runtime->services.diagnostic) runtime->services.diagnostic(runtime->services.context,severity,text);
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
    if (maximum > INT32_MAX || SIZE_MAX / maximum < sizeof(bot_weapon_state))
        return bot_runtime_fail(e, "bot runtime handle capacity overflow");
    qa_bot_runtime *r = calloc(1, sizeof(*r));
    if (!r) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot runtime"); return false; }
    r->options = *options;
    bot_weapon_pointers_init(&r->weapon_pointers);
    r->options.maximum_states = maximum;
    r->services = *services;
    r->characters = calloc(maximum, sizeof(*r->characters));
    r->weapons = calloc(maximum, sizeof(*r->weapons));
    r->chats = calloc(64, sizeof(*r->chats));
    if (!r->characters || !r->weapons || !r->chats) {
        qa_error_set(e, QA_ERROR_MEMORY, maximum, "allocating bot runtime handle tables");
        (void)qa_bot_runtime_destroy(r, NULL);
        return false;
    }
    if (!qa_bot_library_create(&options->library, &r->library, e)) {
        (void)qa_bot_runtime_destroy(r, NULL);
        return false;
    }
    r->library->character_context = r;
    r->library->character_available = bot_runtime_character_available;
    r->library->character_publish = bot_runtime_character_publish;
    r->memory=qa_bot_library_memory(r->library);
    if(!qa_bot_memory_retain(r->memory,e)) {
        r->memory=NULL;(void)qa_bot_runtime_destroy(r,NULL);return false;
    }
    r->globals = (qa_script_defines *)qa_bot_library_global_defines(r->library);
    qa_script_defines_retain(r->globals);
    r->options.library.preprocessor.globals = r->globals;
    if(!qa_bot_weight_workspace_create(&r->weapon_workspace,e)) {
        (void)qa_bot_runtime_destroy(r,NULL);return false;
    }
    qa_bot_log_services log={.context=r,.open=log_open,.print=log_print};
    if(!qa_bot_log_create(&log,&r->log,e)) {
        (void)qa_bot_runtime_destroy(r,NULL);return false;
    }
    if (!qa_bot_library_log_bind(r->library, r->log, e)) {
        (void)qa_bot_runtime_destroy(r, NULL); return false;
    }
    if (!bot_runtime_owners_create(r, e)) {
        (void)qa_bot_runtime_destroy(r, NULL);
        return false;
    }
    *out = r;
    return true;
}
static bool close(qa_bot_runtime *r,bool source,qa_error *error) {
    if(source) {
        r->busy=true;
        bool ok=bot_runtime_chat_shutdown(r,error);
        r->busy=false;
        if(!ok) return false;
    }
    if (!qa_bot_moves_shutdown(r->moves,error)) return false;
    qa_bot_moves_destroy(r->moves); r->moves = NULL;
    if(source && !qa_bot_goals_shutdown(r->goals,error)) return false;
    qa_bot_goals_destroy(r->goals); r->goals = NULL;
    if(source) {
        r->busy=true;
        bool ok=bot_runtime_weapons_shutdown(r,error);
        r->busy=false;
        if(!ok) return false;
    }
    if(source) {
        r->busy=true;
        bool ok=qa_bot_library_weights_shutdown(r->library,error);
        r->busy=false;
        if(!ok) return false;
    }
    if (source) {
        r->busy = true;
        bool okay = bot_runtime_characters_shutdown(r, error);
        r->busy = false;
        if (!okay) return false;
    }
    bot_runtime_handles_close(r);
    qa_bot_chat_system_destroy(r->chat_system); r->chat_system = NULL;
    qa_bot_weapons_release(r->weapon_config); r->weapon_config = NULL;
    qa_bot_actions_shutdown(r->actions);
    if (!r->closed) {
        qa_bot_library_variables_clear(r->library);
        qa_script_defines_clear(r->globals);
        if(source) {bool succeeded;if(!qa_bot_log_close(r->log,&succeeded,error)) return false;}
    }
    qa_bot_library_destroy(r->library); r->library = NULL;
    if(r->memory && !qa_bot_memory_dispose(r->memory,error)) return false;
    if (r->options.observations != QA_BOT_OBSERVATION_MODULE) {
        bot_runtime_observations_close(r);
        qa_bot_bsp_close(r->bsp); r->bsp = NULL;
        r->bsp_loaded = false;
        free(r->map_name); r->map_name = NULL;
        r->map = (qa_bot_runtime_map){0};
    }
    r->initialized = r->library_initialized = r->loaded = false;
    r->closed = true;
    return true;
}
bool qa_bot_runtime_destroy(qa_bot_runtime *r, qa_error *e) {
    if (!r) return true;
    if (!bot_runtime_owners_idle(r, e)) return false;
    (void)close(r,false,NULL);
    qa_bot_log_destroy(r->log);
    qa_bot_actions_destroy(r->actions);
    bot_runtime_observations_close(r);
    qa_bot_bsp_close(r->bsp);
    free(r->map_name);
    free(r->characters); free(r->weapons); free(r->chats);
    qa_bot_weight_workspace_destroy(r->weapon_workspace);
    bot_runtime_weapon_diagnostics_clear(r);
    qa_script_defines_release(r->globals);
    (void)qa_bot_memory_release(r->memory,NULL);
    free(r);
    return true;
}
bool qa_bot_runtime_shutdown(qa_bot_runtime *r, qa_error *e) {
    if (r && r->closed) return true;
    if (!bot_runtime_mutable(r, e)) return false;
    if (!bot_runtime_owners_idle(r, e)) return false;
    return close(r,true,e);
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
qa_bot_memory *qa_bot_runtime_memory(const qa_bot_runtime *r) {return r?r->memory:NULL;}
qa_script_defines *qa_bot_runtime_global_defines(qa_bot_runtime *r) { return r ? r->globals : NULL; }
qa_bot_log *qa_bot_runtime_log(qa_bot_runtime *r) { return r ? r->log : NULL; }
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
    if (!r || r->busy || r->restore_pending || r->observation_leases ||
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
bool qa_bot_runtime_rebind_round(qa_bot_runtime *r, const qa_bot_runtime_map *map, qa_error *e) {
    if (!bot_runtime_mutable(r, e) || !bot_runtime_owners_idle(r, e)) return false;
    if (!map || !map->name || !map->navigation ||
        !r->map_name || strcmp(map->name, r->map_name) || map->entities != r->map.entities ||
        map->source_entities.data != r->map.source_entities.data ||
        map->source_entities.size != r->map.source_entities.size)
        return bot_runtime_fail(e, "bot round must retain its actual map and source metadata");
    const qa_entities *entities = r->bsp ? qa_bot_bsp_entities(r->bsp) : r->map.entities;
    if (!qa_bot_goals_rebind_world(r->goals, r->loaded ? entities : NULL, e)) return false;
    r->map.navigation = map->navigation;
    bot_runtime_observations_clear(r);
    return true;
}
bool qa_bot_runtime_load_map(qa_bot_runtime *r, const char *name, qa_error *e) {
    if (!bot_runtime_owners_idle(r, e)) return false;
    if (r->restore_pending) return bot_runtime_fail(e, "bot runtime continuation is not completely imported");
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
    if (ok) {
        qa_bot_bsp_close(r->bsp);
        r->bsp = bsp;
        bsp = NULL;
        r->bsp_loaded = true;
        bot_runtime_observations_clear(r);
        if (module && (r->closed || !r->library_initialized))
            ok = bot_runtime_fail(e, "bot library is closed or not initialized");
    }
    if (ok) ok = qa_bot_goals_load_map(r->goals, entities, r->map.navigation, e);
    if (ok) {
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
