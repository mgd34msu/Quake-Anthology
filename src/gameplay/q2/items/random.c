#include "internal.h"
#define LIST(a) {a, sizeof(a) / sizeof(*(a))}
static const char *const classic_weapon[] = {"weapon_blaster",
                                             "weapon_shotgun",
                                             "weapon_supershotgun",
                                             "weapon_machinegun",
                                             "weapon_chaingun",
                                             "weapon_etf_rifle",
                                             "weapon_grenadelauncher",
                                             "weapon_proxlauncher",
                                             "weapon_rocketlauncher",
                                             "weapon_hyperblaster",
                                             "weapon_plasmabeam",
                                             "weapon_railgun",
                                             "weapon_bfg",
                                             "weapon_chainfist"};
static const char *const classic_ammo[] = {"ammo_grenades",   "ammo_shells",  "ammo_bullets",
                                           "ammo_cells",      "ammo_rockets", "ammo_slugs",
                                           "ammo_flechettes", "ammo_prox",    "ammo_tesla"};
static const char *const classic_power[] = {"ammo_nuke",
                                            "item_quad",
                                            "item_invulnerability",
                                            "item_silencer",
                                            "item_breather",
                                            "item_enviro",
                                            "item_ir_goggles",
                                            "item_double",
                                            "item_compass",
                                            "item_sphere_vengeance",
                                            "item_sphere_hunter",
                                            "item_sphere_defender",
                                            "item_doppleganger"};
static const char *const classic_key[] = {"key_data_cd",
                                          "key_power_cube",
                                          "key_pyramid",
                                          "key_data_spinner",
                                          "key_pass",
                                          "key_blue_key",
                                          "key_red_key",
                                          "key_commander_head",
                                          "key_airstrike_target",
                                          "key_nuke_container",
                                          "key_nuke"};
static const char *const rr_weapon[] = {
    "weapon_chainfist",       "weapon_shotgun",      "weapon_supershotgun",
    "weapon_machinegun",      "weapon_etf_rifle",    "weapon_chaingun",
    "weapon_grenadelauncher", "weapon_proxlauncher", "weapon_rocketlauncher",
    "weapon_hyperblaster",    "weapon_boomer",       "weapon_plasmabeam",
    "weapon_railgun",         "weapon_phalanx",      "weapon_bfg",
    "weapon_disintegrator"};
static const char *const rr_ammo[] = {"ammo_grenades",   "ammo_trap",    "ammo_tesla",
                                      "ammo_shells",     "ammo_bullets", "ammo_cells",
                                      "ammo_rockets",    "ammo_slugs",   "ammo_magslug",
                                      "ammo_flechettes", "ammo_prox",    "ammo_disruptor"};
static const char *const rr_power[] = {"ammo_nuke",          "item_quad",
                                       "item_quadfire",      "item_invulnerability",
                                       "item_invisibility",  "item_silencer",
                                       "item_breather",      "item_enviro",
                                       "item_adrenaline",    "item_bandolier",
                                       "item_pack",          "item_ir_goggles",
                                       "item_double",        "item_sphere_vengeance",
                                       "item_sphere_hunter", "item_sphere_defender",
                                       "item_doppleganger",  "item_health_mega"};
static const char *const rr_key[] = {
    "key_data_cd",          "key_power_cube",     "key_explosive_charges",
    "key_yellow_key",       "key_power_core",     "key_pyramid",
    "key_data_spinner",     "key_pass",           "key_blue_key",
    "key_red_key",          "key_green_key",      "key_commander_head",
    "key_airstrike_target", "key_nuke_container", "key_nuke"};
typedef struct choices {
    const char *const *names;
    size_t count;
} choices;
static const choices classic[] = {LIST(classic_weapon), LIST(classic_ammo), LIST(classic_power),
                                  LIST(classic_key)};
static const choices rerelease[] = {LIST(rr_weapon), LIST(rr_ammo), LIST(rr_power), LIST(rr_key)};
#undef LIST
static const choices *category(const choices *table, const char *name) {
    for (unsigned c = 0; c < 4; ++c)
        for (size_t i = 0; i < table[c].count; ++i)
            if (!strcmp(name, table[c].names[i]))
                return &table[c];
    return NULL;
}
static size_t pick(qa_q2_game *g, size_t count) { return q2_random_bounded(g, (uint32_t)count); }
static const char *replacement(qa_q2_game *g, const qa_q2_item_definition *d) {
    const qa_q2_item_options *o = &g->item_runtime->options;
    if (!o->random_items)
        return NULL;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    const char *name = d->classname;
    if (!rr) {
        if (g->options.product != QA_Q2_ROGUE || d->kind == QA_Q2_ITEM_POWER_ARMOR ||
            d->kind == QA_Q2_ITEM_SHARD)
            return NULL;
        if (d->kind == QA_Q2_ITEM_HEALTH || !strcmp(name, "item_adrenaline")) {
            if (!strcmp(name, "item_health_small"))
                return NULL;
            float chance = q2_random(g);
            return chance < .6f    ? "item_health"
                   : chance < .9f  ? "item_health_large"
                   : chance < .99f ? "item_adrenaline"
                                   : "item_health_mega";
        }
        if (d->kind == QA_Q2_ITEM_ARMOR) {
            float chance = q2_random(g);
            return chance < .6f   ? "item_armor_jacket"
                   : chance < .9f ? "item_armor_combat"
                                  : "item_armor_body";
        }
        const choices *c = category(classic, name);
        if (!c)
            return NULL;
        if ((o->no_spheres &&
             (!strcmp(name, "item_sphere_vengeance") || !strcmp(name, "item_sphere_hunter") ||
              !strcmp(name, "item_spehre_defender"))) ||
            (o->no_nukes && !strcmp(name, "ammo_nuke")) ||
            (o->no_mines && (!strcmp(name, "ammo_prox") || !strcmp(name, "ammo_tesla"))))
            return NULL;
        size_t n = (size_t)ceilf(q2_random(g) * (float)c->count);
        return n ? c->names[n - 1] : NULL;
    }
    if (!strcmp(name, "item_health_small") || d->kind == QA_Q2_ITEM_SHARD)
        return pick(g, 2) ? "item_armor_shard" : "item_health_small";
    if (!strcmp(name, "item_health") || !strcmp(name, "item_health_large"))
        return q2_random(g) < .6f ? "item_health" : "item_health_large";
    if (d->kind == QA_Q2_ITEM_ARMOR || d->kind == QA_Q2_ITEM_POWER_ARMOR) {
        float chance = q2_random(g);
        return chance < .4f   ? "item_armor_jacket"
               : chance < .6f ? "item_armor_combat"
               : chance < .8f ? "item_armor_body"
               : chance < .9f ? "item_power_screen"
                              : "item_power_shield";
    }
    if (!strcmp(name, "item_ancient_head") || !strcmp(name, "item_legacy_head")) {
        static const char *const health[] = {"item_health_small", "item_health",
                                             "item_health_large"};
        return health[pick(g, 3)];
    }
    const choices *c = !strcmp(name, "weapon_blaster") || !strcmp(name, "weapon_grapple")
                           ? &rerelease[0]
                           : category(rerelease, name);
    if (!c)
        return NULL;
    const char *allowed[32];
    size_t count = 0;
    for (size_t i = 0; i < c->count; ++i) {
        const char *candidate = c->names[i];
        if ((o->no_spheres && !strncmp(candidate, "item_sphere_", 12)) ||
            (o->no_nukes && !strcmp(candidate, "ammo_nuke")) ||
            (o->no_mines &&
             (!strcmp(candidate, "ammo_prox") || !strcmp(candidate, "ammo_tesla") ||
              !strcmp(candidate, "ammo_trap") || !strcmp(candidate, "weapon_proxlauncher"))))
            continue;
        allowed[count++] = candidate;
    }
    return count ? allowed[pick(g, count)] : NULL;
}
bool q2_item_randomize(qa_q2_game *g, q2_actor **actor, qa_error *e) {
    q2_actor *a = *actor;
    const char *name = replacement(g, a->item->definition);
    if (!name)
        return true;
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, name);
    if (!d) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 random pickup lacks its native catalog entry: %s",
                     name);
        return false;
    }
    if (g->options.edition == QA_Q2_CLASSIC) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        qa_actor_definition definition;
        if (!qa_builtin_resource(&g->services, d->classname, &definition, e))
            return false;
        qa_actor_id id;
        if (!qa_builtin_spawn_actor(&g->services,
                                    &(qa_builtin_spawn){.owner = g->options.owner,
                                                        .definition = definition,
                                                        .body = body},
                                    &id, e))
            return false;
        bool handled;
        if (!qa_q2_item_spawn_actor(g, id, &(qa_q2_item_spawn){.classname = d->classname}, &handled,
                                    e)) {
            if (q2_actor_live(g, id))
                qa_session_release(g->services.session, id, NULL);
            return false;
        }
        if (!q2_actor_live(g, id))
            return qa_session_release(g->services.session, a->id, e);
        q2_actor *next = q2_actor_get(g, id, false, e);
        if (!next)
            return false;
        next->item->visual.render_flags |= 0x8000;
        if (!qa_session_release(g->services.session, a->id, e))
            return false;
        *actor = next;
        return true;
    }
    a->item->definition = d;
    a->item->spawn.classname = d->classname;
    a->item->visual.effects = d->rotate ? 1 : 0;
    if (!qa_builtin_resource(&g->services, d->model, &a->item->visual.models[0], e))
        return false;
    return q2_item_visual(g, a, e);
}
