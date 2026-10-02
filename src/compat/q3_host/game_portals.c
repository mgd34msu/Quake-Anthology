#include "internal.h"

static int compare_portal(const void *a, const void *b)
{
    uint32_t first = *(const uint32_t *)a, second = *(const uint32_t *)b;
    return (first > second) - (first < second);
}

bool q3_game_portal_reference(qa_q3_host *host, uint32_t first, uint32_t second,
                              q3_portal_reference *out, qa_error *error)
{
    qa_collision_geometry *geometry = qa_world_geometry(host->options.world);
    if (first >= second || second >= qa_collision_area_count(geometry) ||
        qa_collision_geometry_family(geometry) == QA_COLLISION_Q1)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 portal reference has no area pair in this map");
    q3_portal_reference entry = {.first = first, .second = second};
    if (qa_collision_geometry_family(geometry) == QA_COLLISION_Q3) { *out = entry; return true; }
    const qa_bsp_view *bsp = qa_collision_bsp(geometry);
    qa_bsp_area area;
    if (!qa_bsp_read_area(bsp, first, &area, error)) return false;
#if SIZE_MAX <= UINT32_MAX
    if (area.portals.count > SIZE_MAX / sizeof(uint32_t))
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 foreign area portal table is too large");
#endif
    uint32_t *ids = area.portals.count ? malloc((size_t)area.portals.count * sizeof(*ids)) : NULL;
    if (area.portals.count && !ids) return q3_fail(error, QA_ERROR_MEMORY, 0, "Resolving Q3 foreign area portals");
    size_t count = 0;
    for (uint32_t i = 0; i < area.portals.count; ++i) {
        qa_bsp_area_portal portal;
        if (!qa_bsp_read_area_portal(bsp, (size_t)area.portals.first + i, &portal, error)) { free(ids); return false; }
        if (portal.other_area == second) ids[count++] = portal.portal;
    }
    if (count > 1) qsort(ids, count, sizeof(*ids), compare_portal);
    entry.targets = count ? malloc(count * sizeof(*entry.targets)) : NULL;
    if (count && !entry.targets) { free(ids); return q3_fail(error, QA_ERROR_MEMORY, 0, "Retaining Q3 portal targets"); }
    for (size_t i = 0; i < count; ++i) {
        if (entry.target_count && entry.targets[entry.target_count - 1].portal == ids[i])
            ++entry.targets[entry.target_count - 1].multiplicity;
        else entry.targets[entry.target_count++] = (q3_portal_target){ids[i], 1};
    }
    free(ids); *out = entry; return true;
}

static bool apply_pair(qa_q3_host *host, const q3_portal_reference *entry, bool open, qa_error *error)
{
    qa_collision_geometry *geometry = qa_world_geometry(host->options.world);
    if (qa_collision_geometry_family(geometry) == QA_COLLISION_Q3)
        return qa_collision_adjust_area_pair(geometry, (int32_t)entry->first, (int32_t)entry->second, open, error);
    for (size_t i = 0; i < entry->target_count; ++i) {
        q3_portal_target target = entry->targets[i]; bool primary; uint32_t count;
        if (!qa_collision_portal_state(geometry, target.portal, &primary, &count, error)) return false;
        if (target.multiplicity > INT32_MAX || (open ? count > UINT32_MAX - target.multiplicity : count < target.multiplicity))
            return q3_fail(error, QA_ERROR_ARGUMENT, target.portal, "Q3 foreign portal reference overflow or underflow");
    }
    for (size_t i = 0; i < entry->target_count; ++i) {
        q3_portal_target target = entry->targets[i];
        if (!qa_collision_adjust_portal(geometry, target.portal,
                open ? (int)target.multiplicity : -(int)target.multiplicity, error)) return false;
    }
    return true;
}

bool q3_game_portal(qa_q3_host *host, int32_t first, int32_t second, bool open, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || !host->game)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 portal operation requires a game host");
    if (first < 0 || second < 0 || first == second ||
        qa_collision_geometry_family(qa_world_geometry(host->options.world)) == QA_COLLISION_Q1) return true;
    q3_game_data *game = host->game;
    uint32_t a = (uint32_t)(first < second ? first : second), b = (uint32_t)(first < second ? second : first);
    size_t index = 0;
    while (index < game->portal_count && (game->portals[index].first != a || game->portals[index].second != b)) ++index;
    uint32_t previous = index == game->portal_count ? 0 : game->portals[index].count;
    if (open ? previous == UINT32_MAX : previous == 0)
        return q3_fail(error, QA_ERROR_ARGUMENT, index, "Q3 private portal reference overflow or underflow");
    if (index == game->portal_count && game->portal_count == game->portal_capacity) {
        size_t capacity = game->portal_capacity ? game->portal_capacity * 2 : 8;
        if (capacity < game->portal_capacity || capacity > SIZE_MAX / sizeof(*game->portals))
            return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 portal reference table is too large");
        void *grown = realloc(game->portals, capacity * sizeof(*game->portals));
        if (!grown) return q3_fail(error, QA_ERROR_MEMORY, 0, "Retaining Q3 portal references");
        game->portals = grown; game->portal_capacity = capacity;
    }
    q3_portal_reference created = {0};
    if (index == game->portal_count && !q3_game_portal_reference(host, a, b, &created, error)) return false;
    if (!apply_pair(host, index == game->portal_count ? &created : game->portals + index, open, error)) {
        free(created.targets); return false;
    }
    if (index == game->portal_count) {
        game->portals[index] = created; ++game->portal_count;
    }
    game->portals[index].count = open ? previous + 1 : previous - 1; return true;
}

bool q3_game_close_portals(qa_q3_host *host, qa_error *error)
{
    if (!host || !host->game) return true;
    for (size_t i = 0; i < host->game->portal_count; ++i) {
        q3_portal_reference *entry = host->game->portals + i;
        while (entry->count && !host->restore_pending) {
            if (!apply_pair(host, entry, false, error)) return false;
            --entry->count;
        }
        free(entry->targets); entry->targets = NULL; entry->target_count = 0;
    }
    host->game->portal_count = 0; return true;
}

size_t qa_q3_host_portal_claim_count(const qa_q3_host *host)
{
    if (!host || host->retired || !host->game) return 0;
    bool native = qa_collision_geometry_family(qa_world_geometry(host->options.world)) == QA_COLLISION_Q3;
    size_t count = 0;
    for (size_t i = 0; i < host->game->portal_count; ++i)
        if (host->game->portals[i].count) count += native ? 1 : host->game->portals[i].target_count;
    return count;
}

bool qa_q3_host_portal_claim_at(const qa_q3_host *host, size_t index, qa_q3_host_portal_claim *out)
{
    if (!host || host->retired || !host->game || !out) return false;
    qa_collision_geometry *geometry = qa_world_geometry(host->options.world);
    qa_collision_family family = qa_collision_geometry_family(geometry);
    for (size_t i = 0; i < host->game->portal_count; ++i) {
        const q3_portal_reference *entry = host->game->portals + i;
        if (!entry->count) continue;
        size_t count = family == QA_COLLISION_Q3 ? 1 : entry->target_count;
        if (index >= count) { index -= count; continue; }
        *out = (qa_q3_host_portal_claim){.map_identity = qa_collision_map_identity(geometry), .family = family,
            .first = entry->first, .second = entry->second, .contributions = entry->count};
        if (family == QA_COLLISION_Q2) {
            out->portal = entry->targets[index].portal;
            out->contributions *= entry->targets[index].multiplicity;
        }
        return true;
    }
    return false;
}
