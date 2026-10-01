#include "internal.h"
#include "../goals/internal.h"
#include "qa/bots_log_consumers.h"

static bool command(void *context, int32_t client, const char *text, qa_error *e) {
    qa_bot_runtime *r = context;
    bool prior=r->busy; r->busy=true;
    bool ok=r->services.command ? r->services.command(r->services.context, client, text, e) :
        bot_runtime_fail(e, "bot command service is not bound");
    r->busy=prior;
    return ok;
}
static void diagnostic(void *context, qa_script_severity severity, const char *text) {
    qa_bot_runtime *r = context;
    bool prior=r->busy; r->busy=true;
    if (r->services.diagnostic) r->services.diagnostic(r->services.context, severity, text);
    r->busy=prior;
}
static bool test_initial(void *context) {
    qa_bot_runtime *r=context;
    const qa_bot_variable *v=qa_bot_library_variable(r->library,"bot_testichat");
    return v && v->value!=0;
}
static bool test_reply(void *context) {
    qa_bot_runtime *r=context;
    const qa_bot_variable *v=qa_bot_library_variable(r->library,"bot_testrchat");
    return v && v->value!=0;
}
static qa_bot_navigation *navigation(void *context, int32_t client) {
    return qa_bot_runtime_navigation(context, client);
}
static qa_actor_id actor(void *context, int32_t entity) {
    qa_bot_runtime *r = context;
    if (r->services.movement.actor) {
        bool previous = r->busy; r->busy = true;
        qa_actor_id result = r->services.movement.actor(r->services.movement.context, entity);
        r->busy = previous;
        return result;
    }
    const qa_bot_entity_info *info = bot_runtime_observation(r, entity);
    return info ? info->state.actor : (qa_actor_id){0};
}
static int32_t entity_number(void *context, qa_actor_id id) {
    qa_bot_runtime *r = context;
    if (r->services.movement.entity_number) {
        bool previous = r->busy; r->busy = true;
        int32_t result = r->services.movement.entity_number(r->services.movement.context, id);
        r->busy = previous;
        return result;
    }
    qa_bot_entity_info info;
    int32_t previous, *cursor = NULL;
    bool found;
    while (qa_bot_runtime_entity_after(r, cursor, &info, &found, NULL) && found) {
        if (info.valid && qa_actor_id_equal(info.state.actor, id)) return info.number;
        previous = info.number;
        cursor = &previous;
    }
    return 1023;
}
static int32_t entity_model(void *context, int32_t entity) {
    qa_bot_runtime *r = context;
    if (r->services.movement.entity_model) {
        bool previous = r->busy; r->busy = true;
        int32_t result = r->services.movement.entity_model(r->services.movement.context, entity);
        r->busy = previous;
        return result;
    }
    const qa_bot_entity_info *info = bot_runtime_observation(r, entity);
    return info ? info->state.model_index : 0;
}
static int32_t entity_type(void *context, int32_t entity) {
    qa_bot_runtime *r = context;
    if (r->services.movement.entity_type) {
        bool previous = r->busy; r->busy = true;
        int32_t result = r->services.movement.entity_type(r->services.movement.context, entity);
        r->busy = previous;
        return result;
    }
    const qa_bot_entity_info *info = bot_runtime_observation(r, entity);
    return info ? info->state.type : 0;
}
static int32_t entity_weapon(void *context, int32_t entity) {
    qa_bot_runtime *r = context;
    if (r->services.movement.entity_weapon) {
        bool previous = r->busy; r->busy = true;
        int32_t result = r->services.movement.entity_weapon(r->services.movement.context, entity);
        r->busy = previous;
        return result;
    }
    const qa_bot_entity_info *info = bot_runtime_observation(r, entity);
    return info ? info->state.weapon : 0;
}
static int32_t next_entity(void *context, int32_t after) {
    qa_bot_runtime *r = context;
    if (!r->services.movement.next_entity) return qa_bot_runtime_next_entity(r, after);
    bool previous = r->busy; r->busy = true;
    int32_t result = r->services.movement.next_entity(r->services.movement.context, after);
    r->busy = previous;
    return result;
}
static bool model(void *context, int32_t id, qa_bot_travel_model *out, bool *found, qa_error *e) {
    qa_bot_runtime *r = context;
    if (r->services.movement.model) {
        bool previous = r->busy; r->busy = true;
        bool ok = r->services.movement.model(r->services.movement.context, id, out, found, e);
        r->busy = previous;
        return ok;
    }
    *found = false;
    return true;
}
static bool travel_weapon(void *context, int32_t client, qa_nav_travel mode,
                            int32_t *out, bool *found, qa_error *e) {
    qa_bot_runtime *r = context;
    if (r->services.movement.travel_weapon) {
        bool previous = r->busy; r->busy = true;
        bool ok = r->services.movement.travel_weapon(r->services.movement.context, client, mode, out, found, e);
        r->busy = previous;
        return ok;
    }
    *found = false;
    return true;
}
static bool grapple_observation(void *context, int32_t client,
                                  qa_bot_grapple_observation *out, qa_error *e) {
    qa_bot_runtime *r = context;
    if (r->services.movement.grapple_state) {
        bool previous = r->busy; r->busy = true;
        bool ok = r->services.movement.grapple_state(r->services.movement.context, client, out, e);
        r->busy = previous;
        return ok;
    }
    *out = QA_BOT_GRAPPLE_NONE;
    if (r->options.observations != QA_BOT_OBSERVATION_MODULE) return true;
    int32_t selected, missile; bool found;
    if (!travel_weapon(r, client, QA_NAV_GRAPPLE, &selected, &found, e)) return false;
    if (!found && !bot_runtime_integer(r, "weapindex_grapple", "10", &selected, e)) return false;
    if (!bot_runtime_integer(r, "entitytypemissile", "3", &missile, e)) return false;
    for (int32_t entity = next_entity(r, 0); entity; entity = next_entity(r, entity))
        if (entity_type(r, entity) == missile && entity_weapon(r, entity) == selected) {
            *out = QA_BOT_GRAPPLE_FLYING;
            break;
        }
    return true;
}
static qa_bot_move_services movement_services(qa_bot_runtime *r) {
    return (qa_bot_move_services){.context = r, .navigation = navigation,
        .actor = actor, .entity_number = entity_number, .model = model, .entity_model = entity_model,
        .next_entity = next_entity, .entity_type = entity_type, .entity_weapon = entity_weapon,
        .travel_weapon = travel_weapon,
        .grapple_state = r->services.movement.grapple_state || r->options.observations == QA_BOT_OBSERVATION_MODULE ?
            grapple_observation : NULL,
        .diagnostic = diagnostic, .random = r->services.random};
}
bool bot_runtime_owners_create(qa_bot_runtime *r, qa_error *e) {
    qa_bot_action_services actions = {.context = r, .command = command};
    if (!qa_bot_actions_create(0, &actions, &r->actions, e)) return false;
    qa_bot_chat_options options = {.debug = r->options.debug, .console_unavailable = true};
    qa_bot_chat_services chat = {.context = r, .command = command, .diagnostic = diagnostic,
        .test_initial = test_initial, .test_reply = test_reply, .random = r->services.random};
    if (!qa_bot_chat_system_create(&chat, &options, &r->chat_system, e)) return false;
    if (!qa_bot_chat_system_log_bind(r->chat_system, r->log, e)) return false;
    qa_bot_items_view empty = {.path = ""};
    qa_bot_items *items;
    if (!qa_bot_items_restore(&empty, &items, e)) return false;
    qa_bot_goal_options goals = {.maximum_states = r->options.maximum_states,
        .maximum_level_items = 256, .dropped_weight = 1000, .random = r->services.random};
    qa_bot_goal_services goal_services = bot_runtime_goal_services(r);
    bool ok = qa_bot_goals_create(items, &goals, &goal_services, &r->goals, e);
    qa_bot_items_release(items);
    if (!ok) return false;
    if(!bot_goal_memory_bind(r->goals,r->memory,e)) return false;
    if (!qa_bot_goals_reconfigure(r->goals, NULL, 0, 0, e)) return false;
    qa_bot_move_services movement = movement_services(r);
    return qa_bot_moves_create(r->options.maximum_states, r->library, r->actions,
                                 &movement, &r->moves, e);
}
static bool source_failure(qa_bot_runtime *r, const qa_error *e) {
    if (e->code != QA_ERROR_FORMAT && e->code != QA_ERROR_NOT_FOUND) return false;
    diagnostic(r, QA_SCRIPT_ERROR, e->message);
    return true;
}
static bool capacity(qa_bot_runtime *r, const char *name, int32_t *out, qa_error *e) {
    if (!bot_runtime_integer(r, name, "32", out, e)) return false;
    if (*out > 0) return true;
    *out = 32;
    return qa_bot_library_variable_set(r->library, name, "32", e);
}
static bool setup_weapons(qa_bot_runtime *r, int32_t *result, qa_error *e) {
    int32_t weapons, projectiles;
    const qa_bot_variable *path;
    if (!capacity(r, "max_weaponinfo", &weapons, e) || !capacity(r, "max_projectileinfo", &projectiles, e) ||
        !bot_runtime_variable(r, "weaponconfig", "weapons.c", &path, e)) return false;
    qa_bot_weapons *config;
    qa_error local = {0};
    if (!qa_bot_weapons_load(r->library, path->string, (size_t)weapons, (size_t)projectiles, &config, &local)) {
        if (source_failure(r, &local)) { *result = 12; return true; }
        if (e) *e = local;
        return false;
    }
    size_t count = r->options.maximum_states;
    qa_bot_weapon_selector **selectors = calloc(count, sizeof(*selectors));
    if (!selectors) {
        qa_bot_weapons_release(config);
        qa_error_set(e, QA_ERROR_MEMORY, count, "preparing weapon configuration selection maps");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i)
        if (r->weapons[i].weights)
            ok = qa_bot_weapon_selector_create(config, r->weapons[i].weights, &selectors[i], e);
    if (ok) {
        for (size_t i = 0; i < count; ++i) {
            qa_bot_weapon_selector_destroy(r->weapons[i].selector);
            r->weapons[i].selector = selectors[i];
        }
        qa_bot_weapons_release(r->weapon_config);
        r->weapon_config = config;
    } else {
        for (size_t i = 0; i < count; ++i) qa_bot_weapon_selector_destroy(selectors[i]);
        qa_bot_weapons_release(config);
    }
    free(selectors);
    return ok;
}
static bool setup_goals(qa_bot_runtime *r, int32_t *result, qa_error *e) {
    int32_t count, game_type, maximum;
    const qa_bot_variable *path, *dropped;
    if (!bot_runtime_integer(r, "max_iteminfo", "256", &count, e) ||
        !bot_runtime_integer(r, "g_gametype", "0", &game_type, e) ||
        !bot_runtime_integer(r, "max_levelitems", "256", &maximum, e) ||
        !bot_runtime_variable(r, "itemconfig", "items.c", &path, e) ||
        !bot_runtime_variable(r, "droppedweight", "1000", &dropped, e)) return false;
    if (count < 0) {
        count = 256;
        if (!qa_bot_library_variable_set(r->library, "max_iteminfo", "256", e)) return false;
    }
    if (maximum <= 0) return bot_runtime_fail(e, "max_levelitems must be positive");
    qa_bot_items *items; qa_error local = {0};
    if (!qa_bot_items_load(r->library, path->string, (size_t)count, &items, &local)) {
        if (source_failure(r, &local)) {
            if (!qa_bot_goals_reconfigure(r->goals, NULL, 0, 0, e)) return false;
            *result = 10;
            return true;
        }
        if (e) *e = local;
        return false;
    }
    bool ok;
    if (r->goals) ok = qa_bot_goals_reconfigure(r->goals, items, game_type, (uint32_t)maximum, e);
    else {
        qa_bot_goal_options options = {.maximum_states = r->options.maximum_states,
            .maximum_level_items = (uint32_t)maximum, .game_type = game_type,
            .dropped_weight = dropped->value, .random = r->services.random};
        qa_bot_goal_services services = bot_runtime_goal_services(r);
        ok = qa_bot_goals_create(items, &options, &services, &r->goals, e);
        if(ok) ok=bot_goal_memory_bind(r->goals,r->memory,e);
    }
    qa_bot_items_release(items);
    return ok;
}
static bool setup_chat(qa_bot_runtime *r, qa_error *e) {
    static const char *const names[] = {"synfile", "rndfile", "matchfile", "rchatfile"};
    static const char *const defaults[] = {"syn.c", "rnd.c", "match.c", "rchat.c"};
    qa_bot_chat_asset *assets[4] = {0};
    const qa_bot_variable *nochat;
    int32_t maximum;
    if (!bot_runtime_integer(r, "max_messages", "1024", &maximum, e) ||
        !bot_runtime_variable(r, "nochat", "0", &nochat, e)) return false;
    if (maximum < 2) return bot_runtime_fail(e, "max_messages must be at least two");
    bool ok = true;
    for (size_t i = 0; ok && i < 4; ++i) {
        if (i == 3 && nochat->value != 0) continue;
        const qa_bot_variable *path;
        ok = bot_runtime_variable(r, names[i], defaults[i], &path, e);
        if (!ok) break;
        qa_error local = {0};
        if (!qa_bot_chat_asset_load(r->library, (qa_bot_chat_asset_kind)i, path->string, NULL, &assets[i], &local)) {
            ok = source_failure(r, &local);
            if (!ok && e) *e = local;
        }
    }
    if (ok) {
        qa_bot_chat_options options = {.synonyms = assets[0], .randoms = assets[1],
            .matches = assets[2], .replies = assets[3], .console_capacity = (size_t)maximum,
            .debug = r->options.debug};
        qa_bot_chat_services services = {.context = r, .command = command,
            .test_initial=test_initial,.test_reply=test_reply,
            .diagnostic = diagnostic, .random = r->services.random};
        ok = r->chat_system ? qa_bot_chat_system_configure(r->chat_system, &options, e) :
            qa_bot_chat_system_create(&services, &options, &r->chat_system, e);
        if (ok) ok = qa_bot_chat_system_log_bind(r->chat_system, r->log, e);
        const qa_bot_variable *developer=qa_bot_library_variable(r->library,"bot_developer");
        if (ok && assets[3] && developer && developer->value!=0)
            ok=qa_bot_chat_check_integrity(r->chat_system,assets[3],e);
    }
    for (size_t i = 0; i < 4; ++i) qa_bot_chat_asset_release(assets[i]);
    return ok;
}
static bool setup(qa_bot_runtime *r, int32_t *result, qa_error *e) {
    bool log_opened;
    if(!qa_bot_log_open(r->log,r->library,"botlib.log",&log_opened,e)) return false;
    if(r->services.log.print) {
        if(!r->services.log.print(r->services.log.context,QA_SCRIPT_INFO,"------- BotLib Initialization -------\n",e)) return false;
    } else if(r->services.diagnostic)
        r->services.diagnostic(r->services.context,QA_SCRIPT_INFO,"------- BotLib Initialization -------\n");
    int32_t clients, entities = 0;
    if (!bot_runtime_integer(r, "maxclients", "128", &clients, e)) return false;
    if (r->options.observations == QA_BOT_OBSERVATION_NATIVE &&
        !bot_runtime_integer(r, "maxentities", "1024", &entities, e)) return false;
    if (clients < 0 || entities < 0)
        return bot_runtime_fail(e, "invalid bot source client/entity capacities");
    if ((uint32_t)clients < r->options.minimum_clients) {
        if (r->options.minimum_clients > INT32_MAX)
            return bot_runtime_fail(e, "canonical bot client capacity exceeds signed range");
        clients = (int32_t)r->options.minimum_clients;
    }
    qa_bot_action_services actions = {.context = r, .command = command};
    if (r->actions ? !qa_bot_actions_setup(r->actions, (uint32_t)clients, e) :
        !qa_bot_actions_create((uint32_t)clients, &actions, &r->actions, e)) return false;
    if (r->options.observations == QA_BOT_OBSERVATION_NATIVE &&
        !bot_runtime_observations_resize(r, (size_t)entities, e)) return false;
    if (!setup_weapons(r, result, e)) return false;
    if (*result) return true;
    if (!setup_goals(r,result,e)) return false;
    if (*result) return true;
    if (!setup_chat(r, e)) return false;
    if (!r->moves) {
        qa_bot_move_services movement = movement_services(r);
        if (!qa_bot_moves_create(r->options.maximum_states, r->library, r->actions,
                                &movement, &r->moves, e)) return false;
    }
    if (!qa_bot_moves_setup(r->moves, e)) return false;
    r->initialized = r->library_initialized = true;
    return true;
}
bool qa_bot_runtime_setup(qa_bot_runtime *r, int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!bot_runtime_owners_idle(r, e)) return false;
    if (!result) return bot_runtime_fail(e, "missing bot setup source result");
    *result = 0;
    r->busy = true;
    bool ok = setup(r, result, e);
    if (ok && r->options.observations == QA_BOT_OBSERVATION_MODULE) r->initialized = *result == 0;
    r->busy = false;
    return ok;
}
