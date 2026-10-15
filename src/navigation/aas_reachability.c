#include "aas_reachability_internal.h"

/* Cold reachability construction follows be_aas_reach.c. Published AAS records
 * remain immutable; only the completed replacement leaves this arena. */
bool aas_reach_exists(const aas_reach_build *work, uint32_t from, uint32_t to) {
    for (const aas_reach_link *link = work->heads[from]; link; link = link->next)
        if (link->reach.area == (int32_t)to) return true;
    return false;
}
bool aas_reach_add(aas_reach_build *work, uint32_t from, qa_aas_reach reach, qa_error *error) {
    if (work->link_count >= INT32_MAX - 1) {
        qa_error_set(error, QA_ERROR_MEMORY, work->link_count,
            "AAS reachability count exceeds its source record range");
        return false;
    }
    aas_reach_link *link = qa_arena_alloc(&work->storage, sizeof(*link),
        _Alignof(aas_reach_link), error);
    if (!link) return false;
    *link = (aas_reach_link){.reach = reach, .next = work->heads[from]};
    work->heads[from] = link;
    ++work->link_count;
    return true;
}
bool aas_reach_integer(double value, int32_t *out, qa_error *error) {
    if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "AAS reachability value exceeds its source integer range");
        return false;
    }
    *out = (int32_t)value;
    return true;
}
bool aas_reach_time(double time, uint16_t *out, qa_error *error) {
    /* The original conversion stores the low sixteen bits of a signed int. */
    int32_t value;
    if (!aas_reach_integer(time, &value, error)) return false;
    *out = (uint16_t)value;
    return true;
}
static bool generated_type(const aas_reach_build *work, int32_t type) {
    /* Authored restriction bits belong to the existing link until a generator
     * can reconstruct their source rule. */
    uint32_t base = (uint32_t)type & UINT32_C(0x00ffffff);
    if (work->geometry && (base == AAS_REACH_TELEPORT_TYPE || base == AAS_REACH_JUMPPAD_TYPE ||
        base == AAS_REACH_ELEVATOR || base == AAS_REACH_BOBBING) &&
        !((uint32_t)type & UINT32_C(0xfc000000))) return true;
    switch (type) {
    case AAS_REACH_WALK: case AAS_REACH_JUMP: case AAS_REACH_BARRIER:
    case AAS_REACH_LADDER: case AAS_REACH_LEDGE:
    case AAS_REACH_SWIM: case AAS_REACH_WATERJUMP:
        return true;
    default:
        return false;
    }
}
static bool prepare(aas_reach_build *work, qa_error *error) {
    size_t areas = work->source->count[QA_AAS_AREAS];
    if (!areas) return true;
    work->heads = qa_arena_alloc(&work->storage, areas * sizeof(*work->heads),
        _Alignof(aas_reach_link *), error);
    work->settings = qa_arena_alloc(&work->storage, areas * sizeof(*work->settings),
        _Alignof(qa_aas_setting), error);
    work->entity_areas = qa_arena_alloc(&work->storage, areas * sizeof(*work->entity_areas),
        _Alignof(uint32_t), error);
    if (!work->heads || !work->settings || !work->entity_areas) return false;
    memset(work->heads, 0, areas * sizeof(*work->heads));
    memcpy(work->settings, work->source->settings, areas * sizeof(*work->settings));
    if (!qa_aas_query_create(&work->storage, &work->query, error) ||
        !qa_aas_query_prepare(work->query, work->source->count[QA_AAS_NODES], areas, error) ||
        !qa_nav_workspace_create(&work->storage, &work->workspace, error) ||
        !qa_nav_workspace_prepare(work->workspace,
            qa_nav_workspace_limits_read(&work->navigation->graph->view), error))
        return false;
    for (size_t area = 0; area < areas; ++area) {
        const qa_aas_setting *setting = work->source->settings + area;
        /* Preserve the order and full raw fields of every travel group whose
         * generator is not implemented here, including extensions. */
        for (int32_t i = setting->reach_count; i > 0; --i) {
            qa_aas_reach reach = work->source->reachability[setting->first_reach + i - 1];
            if (!generated_type(work, reach.travel_type) &&
                !aas_reach_add(work, (uint32_t)area, reach, error)) return false;
        }
    }
    return true;
}
bool aas_reach_predict(aas_reach_build *work, qa_nav_prediction_query *query, qa_error *error) {
    query->world_only = true;
    query->omit_trajectory = true;
    query->environment = &work->environment;
    return qa_navigation_predict(work->navigation, work->workspace, query, &work->prediction, error);
}
static bool finish(aas_reach_build *work, qa_nav_asset **out, qa_error *error) {
    size_t counts[QA_AAS_LUMP_COUNT];
    memcpy(counts, work->source->count, sizeof(counts));
    counts[QA_AAS_REACHABILITY] = counts[QA_AAS_AREAS] ? work->link_count + 1 : 0;
    /* Cluster numbers depend on the newly constructed reachability. The one
     * existing cluster builder reconstructs them before graph publication. */
    counts[QA_AAS_PORTALS] = counts[QA_AAS_PORTAL_INDEX] = counts[QA_AAS_CLUSTERS] = 0;
    qa_nav_asset *asset = NULL;
    if (!nav_aas_allocate(counts, &asset, error)) return false;
    uint32_t changed = (UINT32_C(1) << QA_AAS_SETTINGS) | (UINT32_C(1) << QA_AAS_REACHABILITY) |
        (UINT32_C(1) << QA_AAS_PORTALS) | (UINT32_C(1) << QA_AAS_PORTAL_INDEX) |
        (UINT32_C(1) << QA_AAS_CLUSTERS);
    nav_aas_copy_records(work->source, &asset->aas, changed);
    qa_aas_reach *reaches = (qa_aas_reach *)asset->aas.reachability;
    size_t next = 0;
    if (counts[QA_AAS_AREAS]) {
        reaches[next++] = work->source->count[QA_AAS_REACHABILITY] ?
            work->source->reachability[0] : (qa_aas_reach){0};
        for (size_t area = 0; area < counts[QA_AAS_AREAS]; ++area) {
            qa_aas_setting *setting = work->settings + area;
            setting->first_reach = (int32_t)next;
            setting->reach_count = 0;
            setting->cluster = setting->cluster_area = 0;
            for (const aas_reach_link *link = work->heads[area]; link; link = link->next) {
                reaches[next++] = link->reach;
                ++setting->reach_count;
            }
        }
        memcpy((void *)asset->aas.settings, work->settings,
            counts[QA_AAS_SETTINGS] * sizeof(*work->settings));
    }
    if (!qa_aas_validate(&asset->aas, error)) {
        qa_nav_asset_release(asset);
        return false;
    }
    *out = asset;
    return true;
}
bool qa_navigation_aas_reachability_build(qa_navigation *navigation, const qa_bsp_view *geometry,
    float (*variable)(void *, const char *, float), void *context,
    qa_nav_asset **out, qa_error *error) {
    const qa_aas_view *source = navigation ? qa_nav_asset_aas(navigation->graph->view.asset) : NULL;
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "AAS generation requires its source navigation owner");
        return false;
    }
    if (!qa_aas_validate(source, error)) return false;
    aas_reach_build work = {.navigation = navigation, .source = source, .geometry = geometry};
    nav_arena_init(&work.storage, &navigation->storage);
#define READ(field, name, initial) work.movement.field = variable ? variable(context, name, initial) : initial
    READ(gravity, "phys_gravity", 800);
    READ(max_step, "phys_maxstep", 19);
    READ(max_water_jump, "phys_maxwaterjump", 18);
    READ(max_barrier, "phys_maxbarrier", 33);
    READ(start_crouch, "rs_startcrouch", 300);
    READ(water_jump, "rs_waterjump", 400);
    READ(barrier_jump, "rs_barrierjump", 100);
    READ(start_ledge, "rs_startwalkoffledge", 70);
    READ(max_fall_height, "rs_maxfallheight", 0);
    READ(fall_delta5, "phys_falldelta5", 40);
    READ(fall_delta10, "phys_falldelta10", 60);
    READ(fall_damage5, "rs_falldamage5", 300);
    READ(fall_damage10, "rs_falldamage10", 500);
    READ(max_velocity, "phys_maxvelocity", 320);
    READ(max_walk_velocity, "phys_maxwalkvelocity", 320);
    READ(jump_velocity, "phys_jumpvel", 270);
    READ(max_jump_fall_height, "rs_maxjumpfallheight", 450);
    READ(start_jump, "rs_startjump", 300);
    READ(teleport, "rs_teleport", 50);
    READ(jump_pad, "rs_jumppad", 250);
    READ(air_jump_pad, "rs_aircontrolledjumppad", 300);
    READ(start_elevator, "rs_startelevator", 50);
    READ(func_bob, "rs_funcbob", 300);
#undef READ
    work.environment = qa_movement_environment_default();
    work.environment.gravity_multiplier = work.movement.gravity / 800;
    work.environment.speed_multiplier = work.movement.max_velocity / 320;
    bool okay = prepare(&work, error);
    for (uint32_t from = 1; okay && from < source->count[QA_AAS_AREAS]; ++from) {
        uint32_t contents = (uint32_t)work.settings[from].contents;
        if (contents & AAS_REACH_JUMPPAD) continue;
        for (uint32_t to = 1; okay && to < source->count[QA_AAS_AREAS]; ++to) {
            if (from == to || aas_reach_exists(&work, from, to)) continue;
            if ((contents & (AAS_REACH_TELEPORT | AAS_REACH_JUMPPAD)) &&
                !(work.settings[to].contents & (AAS_REACH_TELEPORT | AAS_REACH_JUMPPAD))) continue;
            bool found;
            okay = aas_reach_geometry(&work, from, to, &found, error);
        }
    }
    for (uint32_t area = 1; okay && area < source->count[QA_AAS_AREAS]; ++area)
        if (!(work.settings[area].contents & AAS_REACH_JUMPPAD))
            okay = aas_reach_walk_ledge(&work, area, error);
    if (okay && geometry && source->count[QA_AAS_AREAS]) okay = aas_reach_special(&work, error);
    if (okay) okay = finish(&work, out, error);
    qa_nav_prediction_result_free(&work.prediction);
    qa_nav_workspace_destroy(work.workspace);
    qa_entities_free(&work.entities);
    qa_aas_query_destroy(work.query);
    qa_arena_destroy(&work.storage);
    return okay;
}
