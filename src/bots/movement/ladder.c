#include "internal.h"

bool bot_against_ladder(bot_travel *t, bool *out, qa_error *e) {
    *out = false;
    if (!(t->graph->profile.capabilities & QA_NAV_CAPABILITY(QA_NAV_LADDER)))
        return true;
    const qa_aas_view *aas = qa_nav_asset_aas(t->graph->asset);
    qa_vec3 origin = t->state->input.origin;
    if (!aas) {
        qa_point_contents contents;
        if (!qa_bot_navigation_selected_contents(t->navigation, origin, &contents, e))
            return false;
        *out = contents.family == QA_COLLISION_Q2 && (contents.merged & 0x20000000);
        return true;
    }
    static const qa_vec3 offsets[] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, -1, 0}};
    uint32_t number = 0;
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
        if (!qa_bot_navigation_point(t->navigation, qa_vec_add(origin, offsets[i]), &number, e))
            return false;
        if (number)
            break;
    }
    if (!number || !(aas->settings[number].flags & 2) || !(aas->settings[number].presence & 2))
        return true;
    const qa_aas_area *area = &aas->areas[number];
    for (int32_t i = 0; i < area->face_count; ++i) {
        int32_t signed_face = aas->face_index[area->first_face + i];
        const qa_aas_face *face = &aas->faces[signed_face < 0 ? -signed_face : signed_face];
        if (!(face->flags & 2))
            continue;
        const qa_aas_plane *plane = &aas->planes[face->plane ^ (signed_face < 0 ? 1 : 0)];
        if (fabsf(truncf(qa_vec_dot(plane->normal, origin) - plane->distance)) >= 3)
            continue;
        bool inside = true;
        for (int32_t j = 0; j < face->edge_count; ++j) {
            int32_t signed_edge = aas->edge_index[face->first_edge + j];
            const qa_aas_edge *edge = &aas->edges[signed_edge < 0 ? -signed_edge : signed_edge];
            qa_vec3 first = aas->vertices[edge->vertices[signed_edge < 0 ? 1 : 0]];
            qa_vec3 second = aas->vertices[edge->vertices[signed_edge < 0 ? 0 : 1]];
            qa_vec3 side = qa_vec_cross(qa_vec_sub(second, first), aas->planes[face->plane].normal);
            if (qa_vec_dot(qa_vec_sub(origin, first), side) < -.1f) {
                inside = false;
                break;
            }
        }
        if (inside) {
            *out = true;
            break;
        }
    }
    return true;
}
