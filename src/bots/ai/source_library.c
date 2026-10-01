#include "internal.h"
#include "source_library.h"

static bool read_cvar(qa_bots *bots, const char *name, char value[144], qa_error *error)
{
    qa_cvars *registry = bots->services.configuration ?
        bots->services.configuration(bots->services.context) : NULL;
    if (!registry) return bot_ai_fail(error, "Source bot initialization has no actual GAME cvar registry");
    const qa_cvar_view *source = qa_cvars_find(registry, name);
    const char *text = source ? source->value : "";
    size_t length = strlen(text);
    if (length > 143) length = 143;
    memcpy(value, text, length); value[length] = 0;
    return true;
}

static bool set(qa_bots *bots, const char *name, const char *value, qa_error *error)
{
    return qa_bot_library_variable_set(qa_bot_runtime_library(bots->runtime), name, value, error);
}

static bool initialize_library(qa_bots *bots, int32_t *result, qa_error *error)
{
    char value[144], game_type[144];
    if (!read_cvar(bots, "sv_maxclients", value, error) ||
        !set(bots, "maxclients", *value ? value : "8", error) ||
        !set(bots, "maxentities", "1024", error)) return false;
    static const char *const limits[] = {"sv_mapChecksum", "max_aaslinks", "max_levelitems"};
    for (size_t i = 0; i < sizeof(limits) / sizeof(*limits); ++i)
        if (!read_cvar(bots, limits[i], value, error) ||
            (*value && !set(bots, limits[i], value, error))) return false;
    if (!read_cvar(bots, "g_gametype", game_type, error) ||
        !set(bots, "g_gametype", *game_type ? game_type : "0", error) ||
        !set(bots, "bot_developer", bots->source_match.cvars[BOT_SOURCE_DEVELOPER].value, error) ||
        !set(bots, "log", *game_type ? game_type : "0", error) ||
        !read_cvar(bots, "bot_nochat", value, error) ||
        (*value && !set(bots, "nochat", "0", error))) return false;
    static const struct { const char *source, *destination; } forwarding[] = {
        {"bot_visualizejumppads", "bot_visualizejumppads"},
        {"bot_forceclustering", "forceclustering"}, {"bot_forcereachability", "forcereachability"},
        {"bot_forcewrite", "forcewrite"}, {"bot_aasoptimize", "aasoptimize"},
        {"bot_saveroutingcache", "saveroutingcache"}
    };
    for (size_t i = 0; i < sizeof(forwarding) / sizeof(*forwarding); ++i)
        if (!read_cvar(bots, forwarding[i].source, value, error) ||
            (*value && !set(bots, forwarding[i].destination, value, error))) return false;
    if (!read_cvar(bots, "bot_reloadcharacters", value, error) ||
        !set(bots, "bot_reloadcharacters", *value ? value : "0", error)) return false;
    static const struct { const char *source, *destination; } directories[] = {
        {"fs_basepath", "basedir"}, {"fs_game", "gamedir"}, {"fs_cdpath", "cddir"}
    };
    for (size_t i = 0; i < sizeof(directories) / sizeof(*directories); ++i)
        if (!read_cvar(bots, directories[i].source, value, error) ||
            (*value && !set(bots, directories[i].destination, value, error))) return false;
    if (bots->services.team_arena &&
        !qa_bot_library_global_define(qa_bot_runtime_library(bots->runtime), "MISSIONPACK", error)) return false;
    return qa_bot_runtime_setup(bots->runtime, result, error);
}

bool qa_bots_create_source(qa_bot_runtime *runtime, const qa_bot_services *services,
    const char *map, int32_t *result, qa_bots **out, qa_error *error)
{
    if (!runtime || qa_bot_runtime_initialized(runtime) || !services || !services->configuration ||
        !services->register_cvar || !map || !result || !out || *out)
        return bot_ai_fail(error, "Source bot initialization requires its fresh context and actual map");
    *result = 0;
    qa_bots *bots = NULL;
    if (!bot_ai_context_create(runtime, services, 0, &bots, error)) return false;
    bots->busy = true;
    bool okay = qa_bot_runtime_lease_begin(runtime, error);
    if (okay) {
        okay = services->register_cvar &&
            services->register_cvar(services->context, "bot_enable", "1", 0, error) &&
            services->register_cvar(services->context, "g_spSkill", "2", QA_CVAR_ARCHIVE | QA_CVAR_LATCH, error) &&
            bot_ai_source_setup_cvars(bots, error);
        qa_bot_runtime_lease_end(runtime);
    }
    if (okay) okay = initialize_library(bots, result, error);
    if (okay && !*result) {
        bots->client_capacity = qa_bot_actions_capacity(qa_bot_runtime_actions(runtime));
        free(bots->clients); bots->clients = NULL;
        if (bots->client_capacity > SIZE_MAX / sizeof(*bots->clients))
            okay = bot_ai_fail(error, "Initialized bot client aliases exceed their actual allocation extent");
    }
    if (okay && !*result) {
        bots->clients = bots->client_capacity ? calloc(bots->client_capacity, sizeof(*bots->clients)) : NULL;
        if (bots->client_capacity && !bots->clients) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating initialized bot client aliases");
            okay = false;
        }
    }
    if (okay && !*result) {
        char selected_map[256];
        okay = qa_bot_runtime_lease_begin(runtime, error);
        if (okay) {
            okay = services->register_cvar && services->register_cvar(services->context, "mapname", "",
                QA_CVAR_SERVERINFO | QA_CVAR_READONLY, error);
            if (okay) {
                qa_cvars *registry = services->configuration ? services->configuration(services->context) : NULL;
                const qa_cvar_view *selected = registry ? qa_cvars_find(registry, "mapname") : NULL;
                if (!selected || strlen(selected->value) >= sizeof(selected_map))
                    okay = bot_ai_fail(error, "Source bot mapname exceeds its actual cached storage");
                else memcpy(selected_map, selected->value, strlen(selected->value) + 1);
            }
            qa_bot_runtime_lease_end(runtime);
        }
        if (okay) okay = qa_bot_runtime_load_map(runtime, selected_map, error);
        if (okay) {
            okay = qa_bot_runtime_lease_begin(runtime, error);
            if (okay) {
                okay = bot_ai_source_goals_load(bots, error) && bot_ai_source_match_setup(bots, error);
                qa_bot_runtime_lease_end(runtime);
            }
        }
    }
    bots->busy = false;
    if (okay && !*result) { *out = bots; return true; }
    (void)qa_bots_destroy(bots, NULL);
    return okay;
}
