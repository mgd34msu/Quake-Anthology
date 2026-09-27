#include "internal.h"

bool qa_bot_navigation_area_info(const qa_bot_navigation *n, uint32_t area, qa_bot_nav_area_info *out) {
    const qa_nav_node *node = n && area ?
        qa_navigation_node(n->runtime, qa_bot_navigation_node(n, area)) : NULL;
    if (!node || !out) return false;
    *out = (qa_bot_nav_area_info){.area = qa_bot_navigation_area(n, area),
                                 .bounds = node->bounds, .origin = node->origin};
    bool enabled, overridden;
    if (qa_navigation_enabled(n->runtime, node->id, &enabled, &overridden) && overridden)
        out->area.flags = enabled ? out->area.flags & ~8u : out->area.flags | 8;
    return true;
}
bool qa_bot_navigation_reachability_index(qa_bot_navigation *n, const qa_vec3 *point,
                                          int32_t *out, qa_error *e) {
    if (!n || !out) return bot_nav_fail(e, "missing source reachability-index output");
    const qa_nav_graph_view *graph = qa_navigation_graph(n->runtime);
    const qa_aas_view *aas = qa_nav_asset_aas(graph->asset);
    uint32_t area = 0, sum = 0;
    if (point && !qa_bot_navigation_point(n, *point, &area, e)) return false;
    if (point && (!area || !qa_bot_navigation_area(n, area).reach_count)) { *out = 0; return true; }
    if (aas) {
        size_t count = aas->count[QA_AAS_CLUSTERS];
        if (point) {
            const qa_aas_setting *setting = &aas->settings[area];
            if (setting->cluster < 0)
                return bot_nav_fail(e, "source reachability index cannot resolve a negative portal cluster");
            count = (size_t)setting->cluster;
            sum = (uint32_t)setting->cluster_area;
        }
        for (size_t i = 0; i < count; ++i) sum += (uint32_t)aas->clusters[i].reachable_area_count;
    } else {
        uint32_t node = qa_bot_navigation_node(n, area);
        for (size_t i = 0; i < graph->node_count; ++i) {
            if (!qa_navigation_outgoing_count(n->runtime, graph->nodes[i].id)) continue;
            if (point && node == graph->nodes[i].id) break;
            ++sum;
        }
    }
    memcpy(out, &sum, sizeof(sum));
    return true;
}
