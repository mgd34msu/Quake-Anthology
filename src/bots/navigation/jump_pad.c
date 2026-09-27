#include "internal.h"

bool qa_bot_navigation_jump_pad(qa_bot_navigation *n, qa_vec3 origin, qa_bounds bounds,
                                uint32_t *out, qa_error *e) {
    if (!n || !out || !qa_vec_finite(origin))
        return bot_nav_fail(e, "invalid suspended goal query");
    qa_bounds target = qa_bounds_translate(bounds, origin);
    qa_bounds presence = qa_bot_navigation_presence(n, 4);
    const qa_nav_graph_view *g = qa_navigation_graph(n->runtime);
    float volume = 0;
    *out = 0;
    for (size_t i = 0; i < g->edge_count; ++i) {
        const qa_nav_edge *edge = &g->edges[i];
        if (edge->mode != QA_NAV_JUMP_PAD) continue;
        if (!qa_navigation_admit_movement(n->runtime, n->actor, edge->start, edge->end,
                                          edge->mode, &n->trajectory, e))
            return false;
        if (!n->trajectory.found) continue;
        bool touches = false;
        for (size_t j = 0; j < n->trajectory.point_count; ++j) {
            qa_bounds body = qa_bounds_translate(presence, n->trajectory.points[j]);
            if (body.mins.x <= target.maxs.x && body.maxs.x >= target.mins.x &&
                body.mins.y <= target.maxs.y && body.maxs.y >= target.mins.y &&
                body.mins.z <= target.maxs.z && body.maxs.z >= target.mins.z) {
                touches = true;
                break;
            }
        }
        if (!touches) continue;
        const qa_nav_node *node = qa_navigation_node(n->runtime, edge->from);
        if (!node) continue;
        qa_vec3 size = qa_vec_sub(node->bounds.maxs, node->bounds.mins);
        float candidate = (size.x * size.y) * size.z;
        if (candidate >= volume) {
            volume = candidate;
            *out = qa_bot_navigation_source_area(n, edge->from);
        }
    }
    return true;
}
