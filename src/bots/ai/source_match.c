#include "internal.h"
#include "source_event_state.h"
#include "source_match.h"

static const char *const cvar_names[BOT_SOURCE_MATCH_CVARS] = {
    "bot_testsolid", "bot_testclusters", "bot_interbreedchar",
    "bot_interbreedbots", "bot_interbreedcycle", "bot_interbreedwrite",
    "bot_thinktime", "bot_memorydump", "bot_saveroutingcache", "bot_pause",
    "bot_report", "bot_developer", "bot_rocketjump", "bot_grapple", "bot_fastchat",
    "bot_nochat", "bot_testrchat", "bot_challenge", "bot_predictobstacles", "g_spSkill"
};
static void frame_controls(qa_bots *);

static int32_t signed_word(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static size_t decimal(int32_t value, char out[32])
{
    unsigned char reversed[16];
    size_t count = 0;
    int32_t remaining = value < 0 ? signed_word(0u - (uint32_t)value) : value;
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0) reversed[count++] = '-';
    for (size_t index = 0; index < count; ++index)
        out[index] = (char)reversed[count - index - 1];
    out[count] = 0;
    return count;
}

void bot_ai_source_match_init(bot_source_match_globals *state)
{
    *state = (bot_source_match_globals){0};
}

static qa_cvars *configuration(qa_bots *bots, qa_error *error)
{
    qa_cvars *registry = bots->services.configuration
        ? bots->services.configuration(bots->services.context) : NULL;
    if (!registry) bot_ai_fail(error, "source bot match requires its actual GAME cvar registry");
    return registry;
}

static bool cvar_copy(bot_source_match_cvar *cell, const qa_cvar_view *source,
    qa_error *error)
{
    size_t length = strlen(source->value);
    if (length >= sizeof(cell->value))
        return bot_ai_fail(error, "source bot cached cvar exceeds its 256-byte storage");
    memcpy(cell->value, source->value, length + 1);
    cell->numeric_value = source->number;
    cell->integer_value = source->integer;
    cell->modification_count = source->modification_count;
    cell->registered = true;
    return true;
}

bool bot_ai_source_match_register(qa_bots *bots, const char *name, qa_error *error)
{
    for (size_t index = 0; index < BOT_SOURCE_MATCH_CVARS; ++index) {
        if (strcmp(name, cvar_names[index])) continue;
        qa_cvars *registry = configuration(bots, error);
        if (!registry) return false;
        const qa_cvar_view *source = qa_cvars_find(registry, name);
        return source ? cvar_copy(&bots->source_match.cvars[index], source, error)
            : bot_ai_fail(error, "source bot cvar registration has no actual registry cell");
    }
    return true;
}

bool bot_ai_source_match_setup(qa_bots *bots, qa_error *error)
{
    for (size_t index = 0; index < BOT_SOURCE_MATCH_CVARS; ++index)
        if (!bots->source_match.cvars[index].registered)
            return bot_ai_fail(error, "source bot match has no registered cached cvar");
    frame_controls(bots);
    return true;
}

static bool cvar_update(qa_bots *bots, size_t index, qa_error *error)
{
    bot_source_match_cvar *cell = &bots->source_match.cvars[index];
    if (!cell->registered)
        return bot_ai_fail(error, "source bot match reads an unregistered cached cvar");
    qa_cvars *registry = configuration(bots, error);
    if (!registry) return false;
    const qa_cvar_view *source = qa_cvars_find(registry, cvar_names[index]);
    if (!source || source->modification_count == cell->modification_count) return true;
    return cvar_copy(cell, source, error);
}

static bool cvar_set(qa_bots *bots, const char *name, const char *value,
    qa_error *error)
{
    qa_cvars *registry = configuration(bots, error);
    return registry && qa_cvars_set(registry, name, value, true, error);
}

static void frame_controls(qa_bots *bots)
{
    const bot_source_match_cvar *cells = bots->source_match.cvars;
    bots->controls = (qa_bot_controls){
        .think_time_ms = cells[BOT_SOURCE_THINK_TIME].integer_value,
        .paused = cells[BOT_SOURCE_PAUSE].integer_value != 0,
        .challenge = cells[BOT_SOURCE_CHALLENGE].integer_value != 0,
        .fast_chat = cells[BOT_SOURCE_FAST_CHAT].integer_value != 0,
        .no_chat = cells[BOT_SOURCE_NO_CHAT].integer_value != 0,
        .rocket_jump = cells[BOT_SOURCE_ROCKET_JUMP].integer_value != 0,
        .grapple = cells[BOT_SOURCE_GRAPPLE].integer_value != 0,
        .report = cells[BOT_SOURCE_REPORT].integer_value != 0
    };
}

bool bot_ai_source_frame_cvars(qa_bots *bots, qa_error *error)
{
    static const size_t order[] = {
        BOT_SOURCE_ROCKET_JUMP, BOT_SOURCE_GRAPPLE, BOT_SOURCE_FAST_CHAT,
        BOT_SOURCE_NO_CHAT, BOT_SOURCE_TEST_RANDOM_CHAT, BOT_SOURCE_THINK_TIME,
        BOT_SOURCE_MEMORY_DUMP, BOT_SOURCE_SAVE_ROUTING_CACHE, BOT_SOURCE_PAUSE,
        BOT_SOURCE_REPORT
    };
    for (size_t index = 0; index < sizeof(order) / sizeof(*order); ++index)
        if (!cvar_update(bots, order[index], error)) {frame_controls(bots);return false;}
    frame_controls(bots);
    return true;
}

bool bot_ai_source_frame_requests(qa_bots *bots, qa_error *error)
{
    static const size_t order[] = {BOT_SOURCE_MEMORY_DUMP, BOT_SOURCE_SAVE_ROUTING_CACHE};
    for (size_t index = 0; index < sizeof(order) / sizeof(*order); ++index) {
        size_t cell = order[index];
        if (!bots->source_match.cvars[cell].integer_value) continue;
        if (!qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),
            cvar_names[cell] + 4, "1", error) ||
            !cvar_set(bots, cvar_names[cell], "0", error)) return false;
    }
    return true;
}

bool bot_ai_source_frame_limit_think(qa_bots *bots, qa_error *error)
{
    return bots->source_match.cvars[BOT_SOURCE_THINK_TIME].integer_value <= 200 ||
        cvar_set(bots, "bot_thinktime", "200", error);
}

bool bot_ai_source_test_random_chat(qa_bots *bots,int32_t *out,qa_error *error)
{
    if(!cvar_update(bots,BOT_SOURCE_TEST_RANDOM_CHAT,error)) return false;
    *out=bots->source_match.cvars[BOT_SOURCE_TEST_RANDOM_CHAT].integer_value;
    return true;
}

static bot_ai_state *source_state(qa_bots *bots, uint32_t source_client)
{
    uint32_t client = bots->source_clients[source_client];
    bot_ai_state *state = client ? bots->clients[client - 1] : NULL;
    return state && state->inuse && !state->retired && bot_ai_live(bots, state->view.actor) ? state : NULL;
}

static bool state_current(qa_bots *bots, uint32_t client, qa_actor_id actor,
    bot_ai_state **out, qa_error *error)
{
    bot_ai_state *state = source_state(bots, client);
    if (!state || !qa_actor_id_equal(state->view.actor, actor)) {
        bot_ai_fail(error, "source bot genetics lost its selected client generation");
        return false;
    }
    *out = state;
    return true;
}

static float rank(const bot_ai_state *state)
{
    return state ? (float)signed_word((uint32_t)bot_ai_num_kills(state) * 2u -
        (uint32_t)bot_ai_num_deaths(state)) : -1;
}

typedef struct genetic_warning {
    qa_bots *bots;
    qa_error *error;
    bool okay;
} genetic_warning;

static void warning(void *opaque, const char *text)
{
    genetic_warning *scope = opaque;
    scope->okay = scope->bots->services.print
        ? scope->bots->services.print(scope->bots->services.context, text, scope->error)
        : bot_ai_fail(scope->error, "source bot genetics requires its actual Botlib Print service");
}

bool bot_ai_source_interbreed_end_match(qa_bots *bots, qa_error *error)
{
    bot_source_match_globals *globals = &bots->source_match;
    if (!globals->interbreed) return true;
    if (!globals->cvars[BOT_SOURCE_INTERBREED_CYCLE].registered)
        return bot_ai_fail(error, "source bot genetics has no cached cycle cvar");
    globals->interbreed_match_count = signed_word((uint32_t)globals->interbreed_match_count + 1u);
    if (globals->interbreed_match_count < globals->cvars[BOT_SOURCE_INTERBREED_CYCLE].integer_value)
        return true;
    globals->interbreed_match_count = 0;
    if (!cvar_update(bots, BOT_SOURCE_INTERBREED_WRITE, error)) return false;
    if (globals->cvars[BOT_SOURCE_INTERBREED_WRITE].value[0]) {
        bot_ai_state *best = NULL;
        float best_rank = 0;
        for (uint32_t client = 0; client < 64; ++client) {
            bot_ai_state *state = source_state(bots, client);
            float value = rank(state);
            if (value > best_rank) { best_rank = value; best = state; }
        }
        /* The source BotSaveGoalFuzzyLogic validates its state and never writes a file. */
        if (best && !qa_bot_goals_save_weights(qa_bot_runtime_goals(bots->runtime), best->goals, error))
            return false;
        if (!cvar_set(bots, "bot_interbreedwrite", "", error)) return false;
    }
    float ranks[64];
    qa_actor_id actors[64];
    for (uint32_t client = 0; client < 64; ++client) {
        bot_ai_state *state = source_state(bots, client);
        ranks[client] = rank(state);
        actors[client] = state ? state->view.actor : (qa_actor_id){0};
    }
    qa_bot_random_source random = qa_bot_runtime_random_source(bots->runtime);
    genetic_warning scope = {bots, error, true};
    qa_bot_genetic_source source = {.ranks = ranks, .context = &scope, .warning = warning};
    qa_bot_genetic_selection selection;
    if (!qa_bot_genetic_select_from(64, &source, &random, &selection, error) || !scope.okay)
        return false;
    if (selection.status == QA_BOT_GENETIC_SELECTED) {
        bot_ai_state *first, *second, *child;
        if (!state_current(bots, selection.parent1, actors[selection.parent1], &first, error) ||
            !state_current(bots, selection.parent2, actors[selection.parent2], &second, error) ||
            !state_current(bots, selection.child, actors[selection.child], &child, error)) return false;
        bool matched;
        if (!qa_bot_goals_interbreed(qa_bot_runtime_goals(bots->runtime),
            first->goals, second->goals, child->goals, &matched, error) ||
            !state_current(bots, selection.parent1, actors[selection.parent1], &first, error) ||
            !state_current(bots, selection.parent2, actors[selection.parent2], &second, error) ||
            !state_current(bots, selection.child, actors[selection.child], &child, error) ||
            !qa_bot_goals_mutate(qa_bot_runtime_goals(bots->runtime), child->goals, error) ||
            !state_current(bots, selection.child, actors[selection.child], &child, error)) return false;
    }
    for (uint32_t client = 0; client < 64; ++client) {
        bot_ai_state *state = source_state(bots, client);
        if (state) { bot_ai_num_kills_set(state,0); bot_ai_num_deaths_set(state,0); }
    }
    return true;
}

bool bot_ai_source_interbreed_admit(qa_bots *bots, bot_ai_state *state, qa_error *error)
{
    return !bots->source_match.interbreed ||
        qa_bot_goals_mutate(qa_bot_runtime_goals(bots->runtime), state->goals, error);
}

bool bot_ai_source_interbreeding(qa_bots *bots, qa_error *error)
{
    bot_source_match_globals *globals = &bots->source_match;
    if (!cvar_update(bots, BOT_SOURCE_INTERBREED_CHARACTER, error)) return false;
    const char *character = globals->cvars[BOT_SOURCE_INTERBREED_CHARACTER].value;
    if (!character[0]) return true;
    if (bots->source_goals.game_type != 1) {
        if (!cvar_set(bots, "g_gametype", "1", error)) return false;
        if (!bots->services.exit_level)
            return bot_ai_fail(error, "source bot interbreeding has no actual ExitLevel service");
        if (!bots->busy || bots->restore_pending || bots->shutting_down || bots->checking_spawn ||
            bots->source_match_exit_depth == SIZE_MAX || qa_bot_runtime_can_destroy(bots->runtime))
            return bot_ai_fail(error, "source bot ExitLevel has no admitted enclosing AI frame");
        ++bots->source_match_exit_depth;
        bool okay = bots->services.exit_level(bots->services.context, error);
        --bots->source_match_exit_depth;
        return okay;
    }
    for (uint32_t client = 0; client < 64; ++client) {
        bot_ai_state *state = source_state(bots, client);
        if (state && !bot_ai_source_shutdown_client(bots, state, false, error)) return false;
    }
    if (!qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime),
        "bot_reloadcharacters", "1", error)) return false;
    if (!globals->cvars[BOT_SOURCE_INTERBREED_BOTS].registered)
        return bot_ai_fail(error, "source bot interbreeding has no cached bot-count cvar");
    for (int32_t index = 0; index < globals->cvars[BOT_SOURCE_INTERBREED_BOTS].integer_value; ++index) {
        char delay[32], number[32], command[640];
        decimal(signed_word((uint32_t)index * 50u), delay);
        decimal(index, number);
        size_t offset = 0;
        const char *parts[] = {"addbot ", character, " 4 free ", delay, " ", character, number, "\n"};
        for (size_t part = 0; part < sizeof(parts) / sizeof(parts[0]); ++part) {
            size_t length = strlen(parts[part]);
            memcpy(command + offset, parts[part], length); offset += length;
        }
        command[offset] = 0;
        if (!bots->services.insert_console_command)
            return bot_ai_fail(error, "source bot interbreeding has no actual EXEC_INSERT service");
        if (!bots->services.insert_console_command(bots->services.context, command, error)) return false;
    }
    if (!cvar_set(bots, "bot_interbreedchar", "", error)) return false;
    globals->interbreed = true;
    return true;
}

static bool point_area(qa_bot_navigation *navigation, qa_vec3 origin,
    uint32_t *area, qa_error *error)
{
    if (!qa_bot_navigation_point(navigation, origin, area, error)) return false;
    if (*area) return true;
    qa_vec3 end = origin; end.z += 10;
    qa_aas_crossing crossings[10]; size_t count;
    if (!qa_bot_navigation_trace_areas(navigation, origin, end, crossings, 10, &count, error))
        return false;
    *area = count ? crossings[0].area : 0;
    return true;
}

bool bot_ai_source_test_aas(qa_bots *bots, qa_vec3 origin, qa_error *error)
{
    if (!cvar_update(bots, BOT_SOURCE_TEST_SOLID, error) ||
        !cvar_update(bots, BOT_SOURCE_TEST_CLUSTERS, error)) return false;
    bool solid = bots->source_match.cvars[BOT_SOURCE_TEST_SOLID].integer_value != 0;
    if (!solid && !bots->source_match.cvars[BOT_SOURCE_TEST_CLUSTERS].integer_value) return true;
    if (!qa_bot_runtime_initialized(bots->runtime) || !qa_bot_runtime_loaded(bots->runtime)) return true;
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(bots->runtime, -1);
    const qa_nav_graph_view *graph = navigation
        ? qa_navigation_graph(qa_bot_navigation_runtime(navigation)) : NULL;
    if (!graph) return bot_ai_fail(error, "BotTestAAS has no actual initialized navigation owner");
    if (!graph->node_count) return true;
    uint32_t area;
    if (!point_area(navigation, origin, &area, error)) return false;
    const char *text;
    char message[128];
    if (solid) text = area ? "\remtpy area" : "\r^1SOLID area";
    else if (!area) text = "\r^1Solid!                              ";
    else {
        qa_bot_nav_area_info info;
        if (!qa_bot_navigation_area_info(navigation, area, &info))
            return bot_ai_fail(error, "BotTestAAS area is outside actual AAS storage");
        char area_text[32], cluster_text[32];
        decimal(signed_word(area), area_text); decimal(info.area.cluster, cluster_text);
        size_t offset = 0;
        const char *parts[] = {"\rarea ", area_text, ", cluster ", cluster_text, "       "};
        for (size_t part = 0; part < sizeof(parts) / sizeof(parts[0]); ++part) {
            size_t length = strlen(parts[part]);
            memcpy(message + offset, parts[part], length); offset += length;
        }
        message[offset] = 0; text = message;
    }
    return bots->services.print ? bots->services.print(bots->services.context, text, error)
        : bot_ai_fail(error, "BotTestAAS has no actual GAME Print service");
}

bool qa_bots_interbreed_end_admitted(const qa_bots *bots)
{
    return bots && bots->busy && bots->source_match_exit_depth &&
        !bots->restore_pending && !bots->shutting_down && !bots->checking_spawn &&
        qa_bot_runtime_initialized(bots->runtime) && !qa_bot_runtime_can_destroy(bots->runtime);
}

bool qa_bots_interbreed_end_match(qa_bots *bots, qa_error *error)
{
    if (!qa_bots_interbreed_end_admitted(bots) && !bot_ai_mutable(bots, error)) return false;
    if (bots->checking_spawn)
        return bot_ai_fail(error, "source bot match end reentered its spawn callback");
    if (!qa_bot_runtime_lease_begin(bots->runtime, error)) return false;
    bool previous = bots->busy;
    bots->busy = true;
    bool okay = bot_ai_source_interbreed_end_match(bots, error);
    bots->busy = previous;
    qa_bot_runtime_lease_end(bots->runtime);
    return okay;
}

bool qa_bots_test_aas(qa_bots *bots, qa_vec3 origin, qa_error *error)
{
    if (!bot_ai_mutable(bots, error)) return false;
    if (bots->checking_spawn)
        return bot_ai_fail(error, "BotTestAAS reentered its source spawn callback");
    if (!qa_bot_runtime_lease_begin(bots->runtime, error)) return false;
    bots->busy = true;
    bool okay = bot_ai_source_test_aas(bots, origin, error);
    bots->busy = false;
    qa_bot_runtime_lease_end(bots->runtime);
    return okay;
}
