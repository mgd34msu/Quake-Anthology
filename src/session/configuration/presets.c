#include "internal.h"
#include <stdio.h>

static const char *start_map(const qa_product *p)
{
    if (p->family == QA_GAME_Q3) return !strcmp(p->campaign, "missionpack") ? "maps/mpteam1.bsp" : "maps/q3dm0.bsp";
    if (p->family == QA_GAME_Q1) return !strcmp(p->campaign, "ctf") && p->edition == QA_EDITION_CLASSIC ? "maps/ctfstart.bsp" : "maps/start.bsp";
    if (!strcmp(p->campaign, "xatrix")) return "maps/xswamp.bsp";
    if (!strcmp(p->campaign, "rogue")) return "maps/rmine1.bsp";
    if (!strcmp(p->campaign, "ctf")) return "maps/q2ctf1.bsp";
    if (!strcmp(p->campaign, "lmctf")) return "maps/lmctf09.bsp";
    if (!strcmp(p->campaign, "mg2")) return "maps/mguhub.bsp";
    if (!strcmp(p->campaign, "n64")) return "maps/q64/rtest.bsp";
    return "maps/base1.bsp";
}

bool qa_launch_select_original(qa_launch_draft *d, const char *instance, qa_error *error)
{
    const qa_launch_provider *selected = d && instance ? launch_provider(&d->choices, instance) : NULL;
    const qa_product *product = selected ? qa_catalog_product(d->catalog, selected->product) : NULL;
    const qa_product *program = product ? qa_catalog_product(d->catalog, product->program_product) : NULL;
    if (!selected || !product || *selected->component) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original selection requires a selected primary provider");
        return false;
    }
    if (!product->program || !*product->program) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "%s has no installed original module", product->key);
        return false;
    }
    if (!program) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original module lost its installed product");
        return false;
    }
    qa_launch_provider provider = *selected;
    provider.runtime = product->program_kind != QA_PROGRAM_BUILTIN ? product->program_kind :
        product->family == QA_GAME_Q1 ? QA_PROGRAM_QUAKEC :
        product->family == QA_GAME_Q2 ? QA_PROGRAM_NATIVE : QA_PROGRAM_QVM;
    provider.implementation = program->key;
    provider.artifact = product->program;
    return qa_launch_set_provider(d, &provider, error);
}

bool qa_launch_select_game_type(qa_launch_draft *d, const char *component, qa_error *error)
{
    const qa_catalog_mod *mod = d ? qa_catalog_mod_find(d->catalog, component) : NULL;
    const qa_product *product = mod ? qa_catalog_product(d->catalog, mod->product) : NULL;
    if (!mod || mod->purpose != QA_MOD_GAME_TYPE || mod->unavailable || !product ||
        product->availability != QA_CONTENT_INSTALLED) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "game-type selection requires an available installed component");
        return false;
    }
    qa_launch_provider provider = {.instance = "game-type:primary", .product = product->id,
        .runtime = mod->runtime, .implementation = product->key, .artifact = mod->program_path,
        .component = mod->key, .clock = qa_clock_defaults(qa_product_ruleset(product, QA_GAME_Q1))};
    return qa_launch_set_provider(d, &provider, error) &&
        qa_launch_bind(d, &(qa_launch_binding){.scope = {.kind = QA_SCOPE_WORLD},
            .role = QA_ROLE_ENTITIES, .instance = provider.instance}, error);
}

bool launch_defaults(qa_launch_draft *d, qa_product_id product, const char *map, qa_error *error)
{
    const qa_product *p = qa_catalog_product(d->catalog, product);
    if (!p) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unknown native preset product"); return false; }
    const qa_product *program = qa_catalog_product(d->catalog, p->program_product);
    if (!program) program = p;
    bool ctf = !strcmp(p->campaign, "ctf"), lm = !strcmp(p->campaign, "lmctf");
    bool campaign = p->family != QA_GAME_Q3 && p->edition != QA_EDITION_QUAKEWORLD && !ctf && !lm;
    qa_launch_world world = {.preset = product, .geometry = product, .presentation = product,
        .map = map && *map ? map : start_map(p), .campaign = campaign, .doppler = true,
        .skill = p->family == QA_GAME_Q3 ? 2 : 1, .environment = QA_ENVIRONMENT_AUDIO_SOURCE};
    size_t starts_count;
    const qa_catalog_episode *episode;
    const qa_catalog_start *starts = qa_catalog_starts(d->catalog, product, &episode, &starts_count);
    if ((!map || !*map) && starts_count) {
        world.map = starts[0].path; world.start_command = starts[0].bsp;
        /* The episode command may include the original cinematic chain. */
        if (episode && *episode->command) world.start_command = episode->command;
    }
    if (!qa_launch_set_world(d, &world, error)) return false;
    qa_ruleset_id clock = qa_product_ruleset(p, QA_GAME_Q1);
    qa_launch_provider provider = {.instance = "native:primary", .product = product,
        .runtime = p->program_kind, .implementation = program->key,
        .artifact = p->program_kind == QA_PROGRAM_BUILTIN ? NULL : p->program,
        .clock = qa_clock_defaults(clock)};
    if (!qa_launch_set_provider(d, &provider, error)) return false;
    static const qa_launch_role world_roles[] = {QA_ROLE_ENTITIES, QA_ROLE_CAMPAIGN, QA_ROLE_TRANSITION,
        QA_ROLE_ENGINE_BEHAVIOR, QA_ROLE_MONSTERS, QA_ROLE_MODE};
    static const qa_launch_role player_roles[] = {QA_ROLE_MOVEMENT, QA_ROLE_CHARACTER, QA_ROLE_BODY,
        QA_ROLE_SKIN, QA_ROLE_VOICE, QA_ROLE_ARSENAL, QA_ROLE_COMBAT, QA_ROLE_INVENTORY,
        QA_ROLE_PICKUPS, QA_ROLE_HUD, QA_ROLE_EFFECTS, QA_ROLE_AUDIO, QA_ROLE_MUSIC, QA_ROLE_MENU, QA_ROLE_BOTS};
    qa_launch_binding binding = {.scope = {.kind = QA_SCOPE_WORLD}, .instance = provider.instance};
    for (size_t i = 0; i < sizeof(world_roles) / sizeof(world_roles[0]); ++i) {
        binding.role = world_roles[i];
        if ((binding.role == QA_ROLE_CAMPAIGN || binding.role == QA_ROLE_TRANSITION) && !campaign) continue;
        if (!qa_launch_bind(d, &binding, error)) return false;
    }
    binding.scope.kind = QA_SCOPE_DEFAULT_PLAYER;
    for (size_t i = 0; i < sizeof(player_roles) / sizeof(player_roles[0]); ++i) {
        binding.role = player_roles[i]; binding.definition = "";
        if (binding.role == QA_ROLE_BODY) binding.definition = p->family == QA_GAME_Q3 ? "sarge" : p->family == QA_GAME_Q2 ? "male" : "player";
        if (binding.role == QA_ROLE_SKIN) binding.definition = p->family == QA_GAME_Q3 ? "default" : p->family == QA_GAME_Q2 ? "grunt" : "0";
        if (!qa_launch_bind(d, &binding, error)) return false;
    }
    qa_mode_source source = p->family == QA_GAME_Q3 ? (!strcmp(p->campaign, "missionpack") ? QA_MODE_TEAM_ARENA : QA_MODE_Q3)
        : p->family == QA_GAME_Q2 ? (lm ? QA_MODE_LMCTF : ctf ? QA_MODE_Q2_CTF : QA_MODE_Q2)
        : ctf ? QA_MODE_THREEWAVE : !strcmp(p->campaign, "rogue") ? QA_MODE_ROGUE : QA_MODE_Q1;
    qa_mode_kind kind = ctf || lm ? QA_MODE_CTF : campaign ? QA_MODE_SINGLE_PLAYER : QA_MODE_FFA;
    qa_launch_mode mode = {.instance = provider.instance, .rules = qa_mode_defaults(source, kind),
        .teams = {"team:red", "team:blue", "team:neutral"}, .primary_score = true};
    mode.rules.q2_rerelease = p->family == QA_GAME_Q2 && p->edition == QA_EDITION_RERELEASE;
    if (!qa_launch_set_mode(d, &mode, error)) return false;
    qa_launch_equipment equipment = {.scope = {.kind = QA_SCOPE_DEFAULT_PLAYER}, .instance = provider.instance,
        .grapple_source = provider.instance, .grenade_source = "",
        .selection = {.binding = lm ? QA_EQUIPMENT_OFFHAND : QA_EQUIPMENT_WEAPON_SLOT,
            .release_on_teleport = true, .grapple = ctf ? (p->family == QA_GAME_Q1 ? QA_GRAPPLE_THREEWAVE : QA_GRAPPLE_Q2_CTF)
                : lm ? QA_GRAPPLE_LMCTF : QA_GRAPPLE_DISABLED}};
    if (!qa_launch_set_equipment(d, &equipment, error)) return false;
    if (equipment.selection.grapple != QA_GRAPPLE_DISABLED) {
        binding.role = QA_ROLE_EQUIPMENT; binding.definition = "";
        if (!qa_launch_bind(d, &binding, error)) return false;
    }
    return true;
}
