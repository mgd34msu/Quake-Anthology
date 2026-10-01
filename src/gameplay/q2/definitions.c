#include "internal.h"
#include "qa/game_q2_bots.h"

#define B(n) (UINT64_C(1) << (n))
#define D(id, n, i, a, q, w, v, g, p, ac, f, idle, de, pa, fi, r)                                  \
    {id, n, "q2:" i, i, a, "models/" v, "models/" g, q, w, p, ac, f, idle, de, pa, fi, r}
static const qa_q2_weapon_definition base[] = {
    D(QA_Q2_BLASTER, "blaster", "weapon_blaster", NULL, 0, 0, "weapons/v_blast/tris.md2", "", 1, 4,
      8, 52, 55, B(19) | B(32), B(5), false),
    D(QA_Q2_SHOTGUN, "shotgun", "weapon_shotgun", "q2:ammo_shells", 1, 5,
      "weapons/v_shotg/tris.md2", "weapons/g_shotg/tris.md2", 2, 7, 18, 36, 39,
      B(22) | B(28) | B(34), B(8) | B(9), false),
    D(QA_Q2_SUPERSHOTGUN, "supershotgun", "weapon_supershotgun", "q2:ammo_shells", 2, 10,
      "weapons/v_shotg2/tris.md2", "weapons/g_shotg2/tris.md2", 3, 6, 17, 57, 61,
      B(29) | B(42) | B(57), B(7), false),
    D(QA_Q2_MACHINEGUN, "machinegun", "weapon_machinegun", "q2:ammo_bullets", 1, 30,
      "weapons/v_machn/tris.md2", "weapons/g_machn/tris.md2", 4, 3, 5, 45, 49, B(23) | B(45),
      B(4) | B(5), true),
    D(QA_Q2_CHAINGUN, "chaingun", "weapon_chaingun", "q2:ammo_bullets", 1, 60,
      "weapons/v_chain/tris.md2", "weapons/g_chain/tris.md2", 5, 4, 31, 61, 64,
      B(38) | B(43) | B(51) | B(61), (B(22) - 1) & ~(B(5) - 1), true),
    D(QA_Q2_GRENADES, "grenades", "ammo_grenades", "q2:ammo_grenades", 1, 2,
      "weapons/v_handgr/tris.md2", "items/ammo/grenades/medium/tris.md2", 6, 0, 15, 48, 48,
      B(29) | B(34) | B(39) | B(48), B(12), false),
    D(QA_Q2_GRENADELAUNCHER, "grenadelauncher", "weapon_grenadelauncher", "q2:ammo_grenades", 1, 5,
      "weapons/v_launch/tris.md2", "weapons/g_launch/tris.md2", 7, 5, 16, 59, 64,
      B(34) | B(51) | B(59), B(6), false),
    D(QA_Q2_ROCKETLAUNCHER, "rocketlauncher", "weapon_rocketlauncher", "q2:ammo_rockets", 1, 5,
      "weapons/v_rocket/tris.md2", "weapons/g_rocket/tris.md2", 8, 4, 12, 50, 54,
      B(25) | B(33) | B(42) | B(50), B(5), false),
    D(QA_Q2_HYPERBLASTER, "hyperblaster", "weapon_hyperblaster", "q2:ammo_cells", 1, 30,
      "weapons/v_hyperb/tris.md2", "weapons/g_hyperb/tris.md2", 9, 5, 20, 49, 53, 0,
      B(6) | B(7) | B(8) | B(9) | B(10) | B(11), true),
    D(QA_Q2_RAILGUN, "railgun", "weapon_railgun", "q2:ammo_slugs", 1, 5, "weapons/v_rail/tris.md2",
      "weapons/g_rail/tris.md2", 10, 3, 18, 56, 61, B(56), B(4), false),
    D(QA_Q2_BFG, "bfg", "weapon_bfg", "q2:ammo_cells", 50, 50, "weapons/v_bfg/tris.md2",
      "weapons/g_bfg/tris.md2", 11, 8, 32, 55, 58, B(39) | B(45) | B(50) | B(55), B(9) | B(17),
      false),
};
static const qa_q2_weapon_definition xatrix[] = {
    D(QA_Q2_TRAP, "trap", "ammo_trap", "q2:ammo_trap", 1, 1, "weapons/v_trap/tris.md2",
      "weapons/g_trap/tris.md2", 0, 0, 15, 48, 48, B(29) | B(34) | B(39) | B(48), B(12), false),
    D(QA_Q2_IONRIPPER, "ionripper", "weapon_boomer", "q2:ammo_cells", 2, 10,
      "weapons/v_boomer/tris.md2", "weapons/g_boom/tris.md2", 13, 4, 6, 36, 39, B(36), B(5), false),
    D(QA_Q2_PHALANX, "phalanx", "weapon_phalanx", "q2:ammo_magslug", 1, 5,
      "weapons/v_shotx/tris.md2", "weapons/g_shotx/tris.md2", 12, 5, 20, 58, 63,
      B(29) | B(42) | B(55), B(7) | B(8), false),
};
static const qa_q2_weapon_definition rogue[] = {
    D(QA_Q2_TESLA, "tesla", "ammo_tesla", "q2:ammo_tesla", 1, 2, "weapons/v_tesla/tris.md2",
      "ammo/am_tesl/tris.md2", 0, 0, 8, 32, 32, B(21), B(2), false),
    D(QA_Q2_PROXLAUNCHER, "proxlauncher", "weapon_proxlauncher", "q2:ammo_prox", 1, 5,
      "weapons/v_launch/tris.md2", "weapons/g_launch/tris.md2", 15, 5, 16, 59, 64,
      B(34) | B(51) | B(59), B(6), false),
    D(QA_Q2_CHAINFIST, "chainfist", "weapon_chainfist", NULL, 0, 0, "weapons/v_chainf/tris.md2",
      "weapons/g_chainf/tris.md2", 16, 4, 32, 57, 60, 0,
      B(8) | B(9) | B(16) | B(17) | B(18) | B(30) | B(31), false),
    D(QA_Q2_DISINTEGRATOR, "disintegrator", "weapon_disintegrator", "q2:ammo_disruptor", 1, 5,
      "weapons/v_dist/tris.md2", "weapons/g_dist/tris.md2", 12, 4, 9, 29, 34, B(14) | B(19) | B(23),
      B(5), false),
    D(QA_Q2_ETF_RIFLE, "etf_rifle", "weapon_etf_rifle", "q2:ammo_flechettes", 1, 30,
      "weapons/v_etf_rifle/tris.md2", "weapons/g_etf_rifle/tris.md2", 13, 4, 7, 37, 41,
      B(18) | B(28), B(6) | B(7), false),
    D(QA_Q2_HEATBEAM, "heatbeam", "weapon_plasmabeam", "q2:ammo_cells", 2, 10,
      "weapons/v_beamer/tris.md2", "weapons/g_beamer/tris.md2", 14, 8, 12, 39, 44, B(35),
      B(9) | B(10) | B(11) | B(12), false),
};
static const qa_q2_weapon_definition grapples[] = {
    D(QA_Q2_GRAPPLE, "grapple", "weapon_grapple", NULL, 0, 0, "weapons/grapple/tris.md2", "", 12, 5,
      9, 31, 36, B(10) | B(18) | B(27), B(6), false),
    D(QA_Q2_LMCTF_HOOK, "lmctf:hook", "weapon_hook", NULL, 0, 0, "weapons/v_hook/tris.md2",
      "objects/debris2/tris.md2", 11, 9, 13, 34, 38, B(14) | B(18) | B(26) | B(30),
      B(8) | B(9) | B(10) | B(11), true),
    D(QA_Q2_LMCTF_PLASMA, "lmctf:plasma", "weapon_plasma", "q2:ammo_cells", 10, 10,
      "weapons/v_plasma/tris.md2", "weapons/g_plasma/tris.md2", 12, 3, 11, 46, 51, B(16) | B(46),
      B(4) | B(5), false),
};
#undef D
bool qa_q2_weapon_profile_identity(const qa_q2_options *options, qa_q2_weapon weapon,
                                    qa_q2_weapon_identity *out) {
    if (!out) return false;
    *out = (qa_q2_weapon_identity){0};
    if (!options || (unsigned)options->product > QA_Q2_N64 ||
        (unsigned)options->edition > QA_Q2_RERELEASE ||
        (unsigned)options->arsenal_rules > QA_Q2_WEAPON_RULES_LMCTF ||
        (options->arsenal_rules == QA_Q2_WEAPON_RULES_BASE && options->native_hook) ||
        weapon <= QA_Q2_WEAPON_NONE || weapon >= QA_Q2_WEAPON_COUNT)
        return false;
    const qa_q2_weapon_definition *tables[] = {
        base,
        options->product == QA_Q2_XATRIX || options->edition == QA_Q2_RERELEASE ? xatrix : NULL,
        options->product == QA_Q2_ROGUE || options->edition == QA_Q2_RERELEASE ? rogue : NULL,
    };
    const size_t counts[] = {sizeof(base) / sizeof(*base),
        sizeof(xatrix) / sizeof(*xatrix), sizeof(rogue) / sizeof(*rogue)};
    const qa_q2_weapon_definition *definition = NULL;
    for (size_t table = 0; !definition && table < sizeof(tables) / sizeof(*tables); ++table)
        if (tables[table])
            for (size_t i = 0; i < counts[table]; ++i)
                if (tables[table][i].weapon == weapon) { definition = tables[table] + i; break; }
    if (!definition && options->arsenal_rules == QA_Q2_WEAPON_RULES_CTF &&
        options->native_hook && weapon == QA_Q2_GRAPPLE)
        definition = grapples;
    if (!definition && options->arsenal_rules == QA_Q2_WEAPON_RULES_LMCTF) {
        if (weapon == QA_Q2_LMCTF_PLASMA) definition = grapples + 2;
        else if (options->native_hook && weapon == QA_Q2_LMCTF_HOOK) definition = grapples + 1;
    }
    if (!definition) return false;
    *out = (qa_q2_weapon_identity){definition->item, definition->name};
    return true;
}
static void install(qa_q2_game *g, const qa_q2_weapon_definition *d, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (g->definitions[d[i].weapon].name == NULL)
            g->definition_order[g->definition_count++] = d[i].weapon;
        g->definitions[d[i].weapon] = d[i];
    }
}
bool q2_definitions(qa_q2_game *g, qa_error *error) {
    install(g, base, sizeof(base) / sizeof(*base));
    g->definitions[QA_Q2_BLASTER].world_model = "";
    if (g->options.product == QA_Q2_XATRIX || g->options.edition == QA_Q2_RERELEASE)
        install(g, xatrix, sizeof(xatrix) / sizeof(*xatrix));
    if (g->options.product == QA_Q2_ROGUE || g->options.edition == QA_Q2_RERELEASE)
        install(g, rogue, sizeof(rogue) / sizeof(*rogue));
    if (g->options.edition == QA_Q2_RERELEASE) {
        g->definitions[QA_Q2_IONRIPPER].activate_last = 5;
        g->definitions[QA_Q2_IONRIPPER].fire_last = 7;
        g->definitions[QA_Q2_IONRIPPER].fires = B(6);
        g->definitions[QA_Q2_HEATBEAM].repeating = true;
        g->definitions[QA_Q2_HEATBEAM].idle_last = 42;
        g->definitions[QA_Q2_HEATBEAM].deactivate_last = 47;
        g->definitions[QA_Q2_CHAINFIST].repeating = true;
        g->definitions[QA_Q2_ETF_RIFLE].repeating = true;
    }
    for (size_t i = 1; i < QA_Q2_WEAPON_COUNT; ++i) {
        const qa_q2_weapon_definition *d = &g->definitions[i];
        if (d->name == NULL)
            continue;
        if (!qa_builtin_resource(&g->services, d->item, &g->items[i], error) ||
            !qa_builtin_resource(&g->services, d->view_model, &g->view_models[i], error) ||
            (d->ammo != NULL && !qa_builtin_resource(&g->services, d->ammo, &g->ammo[i], error)))
            return false;
    }
    return qa_q2_bot_arsenal_register_multiplayer(g,g->options.arsenal_rules,
        g->options.native_hook,g->options.hook_edition,error) &&
        qa_q2_bot_equipment_register_hook(g,g->options.equipment_hook_rules,
            g->options.equipment_hook_edition,error);
}
bool qa_q2_bot_arsenal_register_multiplayer(qa_q2_game *g,qa_q2_weapon_rules rules,
                                           bool native_hook,qa_q2_edition hook_edition,
                                           qa_error *error) {
    if (!g || (unsigned)rules > QA_Q2_WEAPON_RULES_LMCTF ||
        (unsigned)hook_edition > QA_Q2_RERELEASE ||
        (rules == QA_Q2_WEAPON_RULES_BASE && native_hook)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 source arsenal registration");
        return false;
    }
    if (!native_hook || rules != QA_Q2_WEAPON_RULES_CTF)
        hook_edition = QA_Q2_CLASSIC;
    if (g->arsenal_rules == rules && g->native_hook == native_hook &&
        g->hook_edition == hook_edition)
        return true;
    if (g->item_runtime || g->arsenal_rules != QA_Q2_WEAPON_RULES_BASE || g->continuation_pending ||
        g->continuation_failed || !q2_checkpoint_idle(g,error)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 source arsenal modules already selected");
        return false;
    }
    qa_q2_weapon_definition additions[2];
    uint32_t count = 0;
    if (rules == QA_Q2_WEAPON_RULES_CTF && native_hook) {
        additions[count] = grapples[0];
        additions[count].world_model = "";
        if (hook_edition == QA_Q2_RERELEASE)
            additions[count].fire_last = 10;
        ++count;
    } else if (rules == QA_Q2_WEAPON_RULES_LMCTF) {
        additions[count++] = grapples[2];
        if (native_hook)
            additions[count++] = grapples[1];
    }
    qa_item_id items[2] = {0},ammo[2] = {0};
    qa_string_id models[2] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        const qa_q2_weapon_definition *d = additions + i;
        if (!qa_builtin_resource(&g->services,d->item,items+i,error) ||
            !qa_builtin_resource(&g->services,d->view_model,models+i,error) ||
            (d->ammo && !qa_builtin_resource(&g->services,d->ammo,ammo+i,error)))
            return false;
    }
    install(g,additions,count);
    for (uint32_t i = 0; i < count; ++i) {
        qa_q2_weapon weapon = additions[i].weapon;
        g->items[weapon] = items[i];
        g->ammo[weapon] = ammo[i];
        g->view_models[weapon] = models[i];
    }
    g->arsenal_rules = rules;
    g->native_hook = native_hook;
    g->hook_edition = hook_edition;
    return true;
}
bool qa_q2_bot_equipment_register_hook(qa_q2_game *g,qa_q2_weapon_rules rules,
                                      qa_q2_edition edition,qa_error *error) {
    if (!g || (unsigned)rules > QA_Q2_WEAPON_RULES_LMCTF ||
        (unsigned)edition > QA_Q2_RERELEASE) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 source equipment registration");
        return false;
    }
    if (rules != QA_Q2_WEAPON_RULES_CTF) edition = QA_Q2_CLASSIC;
    if (g->equipment_hook_rules == rules && g->equipment_hook_edition == edition)
        return true;
    if (g->item_runtime || g->equipment_hook_rules != QA_Q2_WEAPON_RULES_BASE ||
        g->continuation_pending || g->continuation_failed || !q2_checkpoint_idle(g,error)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 source equipment already selected");
        return false;
    }
    qa_q2_weapon_definition definition = grapples[rules == QA_Q2_WEAPON_RULES_CTF ? 0 : 1];
    if (rules == QA_Q2_WEAPON_RULES_CTF) {
        definition.world_model = "";
        if (edition == QA_Q2_RERELEASE) definition.fire_last = 10;
    }
    if (g->native_hook && (g->arsenal_rules != rules ||
        (rules == QA_Q2_WEAPON_RULES_CTF && g->hook_edition != edition))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 arsenal and equipment hook definitions conflict");
        return false;
    }
    qa_q2_weapon weapon = definition.weapon;
    if (!g->definitions[weapon].name) {
        qa_item_id item; qa_string_id model;
        if (!qa_builtin_resource(&g->services,definition.item,&item,error) ||
            !qa_builtin_resource(&g->services,definition.view_model,&model,error)) return false;
        g->definitions[weapon] = definition;
        g->items[weapon] = item;
        g->view_models[weapon] = model;
    }
    g->equipment_hook_rules = rules;
    g->equipment_hook_edition = edition;
    return true;
}
const qa_q2_weapon_definition *qa_q2_weapon_definition_at(const qa_q2_game *g,
                                                          qa_q2_weapon weapon) {
    return g != NULL && weapon > QA_Q2_WEAPON_NONE && weapon < QA_Q2_WEAPON_COUNT &&
                   g->definitions[weapon].name != NULL
               ? &g->definitions[weapon]
               : NULL;
}
qa_q2_weapon qa_q2_weapon_from_classname(const qa_q2_game *g, const char *classname) {
    if (g != NULL && classname != NULL)
        for (size_t i = 1; i < QA_Q2_WEAPON_COUNT; ++i)
            if (g->definitions[i].classname != NULL &&
                strcmp(g->definitions[i].classname, classname) == 0)
                return (qa_q2_weapon)i;
    return QA_Q2_WEAPON_NONE;
}
