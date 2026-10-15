#include "asset_internal.h"
#include "qa/stamp.h"

/* be_aas_cluster.c: portal discovery, reachability floods and cluster numbering. */
enum {
    AAS_CLUSTER_PORTAL = 8, AAS_ROUTE_PORTAL = 32, AAS_VIEW_PORTAL = 512,
    AAS_GROUNDED = 1, AAS_FACE_SOLID = 1
};
typedef struct aas_cluster_visit { int32_t area, next; } aas_cluster_visit;
typedef struct aas_clustering {
    const qa_aas_view *source;
    qa_arena storage;
    qa_aas_setting *settings;
    qa_aas_portal *portals;
    qa_aas_cluster *clusters;
    int32_t *portal_index, *portal_for_area;
    size_t portal_count, cluster_count, index_count;
    aas_cluster_visit *stack;
    int32_t *candidate_areas, *front_faces, *back_faces, *front_areas, *back_areas, *pending;
    qa_stamp_set candidates, front_members, back_members, connected, front_edges;
    bool retry;
} aas_clustering;

static void *workspace(aas_clustering *work, size_t count, size_t stride,
    size_t alignment, qa_error *error) {
    if (count > SIZE_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "AAS clustering workspace exceeds addressable storage");
        return NULL;
    }
    return qa_arena_alloc(&work->storage, (count ? count : 1) * stride, alignment, error);
}
static bool marks(aas_clustering *work, qa_stamp_set *set, size_t count, qa_error *error) {
    uint32_t *values = workspace(work, count, sizeof(*values), _Alignof(uint32_t), error);
    if (!values) return false;
    qa_stamp_set_init(set, values, count);
    return true;
}
static bool prepare(aas_clustering *work, qa_error *error) {
    size_t areas = work->source->count[QA_AAS_AREAS];
    size_t faces = 0;
    for (size_t i = 0; i < areas; ++i) {
        size_t count = (size_t)work->source->areas[i].face_count;
        if (count > SIZE_MAX - faces) {
            qa_error_set(error, QA_ERROR_MEMORY, i, "AAS clustering face workspace exceeds addressable storage");
            return false;
        }
        faces += count;
    }
#define ALLOC(field, count, type) do { \
    work->field = workspace(work, count, sizeof(type), _Alignof(type), error); \
    if (!work->field) return false; \
} while (0)
    ALLOC(settings, areas, qa_aas_setting);
    ALLOC(portals, areas + 1, qa_aas_portal);
    ALLOC(clusters, areas + 1, qa_aas_cluster);
    ALLOC(portal_index, areas * 2, int32_t);
    ALLOC(portal_for_area, areas, int32_t);
    ALLOC(stack, areas, aas_cluster_visit);
    ALLOC(candidate_areas, areas, int32_t);
    ALLOC(front_faces, faces, int32_t);
    ALLOC(back_faces, faces, int32_t);
    ALLOC(front_areas, areas, int32_t);
    ALLOC(back_areas, areas, int32_t);
    ALLOC(pending, areas, int32_t);
#undef ALLOC
    memcpy(work->settings, work->source->settings, areas * sizeof(*work->settings));
    return marks(work, &work->candidates, areas, error) &&
        marks(work, &work->front_members, areas, error) &&
        marks(work, &work->back_members, areas, error) &&
        marks(work, &work->connected, areas, error) &&
        marks(work, &work->front_edges, work->source->count[QA_AAS_EDGES], error);
}
static int32_t other_area(const qa_aas_face *face, int32_t area) {
    return face->front_area == area ? face->back_area : face->front_area;
}
static const qa_aas_face *area_face(const aas_clustering *work,
    const qa_aas_area *area, int32_t ordinal, int32_t *number) {
    *number = (int32_t)nav_aas_oriented_index(work->source->face_index[area->first_face + ordinal]);
    return work->source->faces + *number;
}
static size_t adjacent_candidates(aas_clustering *work, int32_t root) {
    qa_stamp_set_begin(&work->candidates);
    qa_stamp_set_mark(&work->candidates, (size_t)root);
    size_t count = 1, depth = 1;
    work->candidate_areas[0] = root;
    work->stack[0] = (aas_cluster_visit){.area = root};
    while (depth) {
        aas_cluster_visit *visit = work->stack + depth - 1;
        const qa_aas_area *area = work->source->areas + visit->area;
        if (visit->next == area->face_count) { --depth; continue; }
        int32_t number;
        const qa_aas_face *face = area_face(work, area, visit->next++, &number);
        if (face->flags & AAS_FACE_SOLID) continue;
        int32_t other = other_area(face, visit->area);
        int32_t presence = work->settings[visit->area].presence;
        int32_t other_presence = work->settings[other].presence;
        if (!(presence & ~other_presence) || (other_presence & ~presence) ||
            !qa_stamp_set_mark(&work->candidates, (size_t)other)) continue;
        work->candidate_areas[count++] = other;
        work->stack[depth++] = (aas_cluster_visit){.area = other};
    }
    return count;
}
static bool connected(aas_clustering *work, const int32_t *areas, size_t count,
    const qa_stamp_set *members) {
    if (!count) return false;
    qa_stamp_set_begin(&work->connected);
    qa_stamp_set_mark(&work->connected, (size_t)areas[0]);
    size_t depth = 1, visited = 1;
    work->pending[0] = areas[0];
    while (depth) {
        int32_t current = work->pending[--depth];
        const qa_aas_area *area = work->source->areas + current;
        for (int32_t i = 0; i < area->face_count; ++i) {
            int32_t number;
            const qa_aas_face *face = area_face(work, area, i, &number);
            if (face->flags & AAS_FACE_SOLID) continue;
            int32_t other = other_area(face, current);
            if (!qa_stamp_set_test(members, (size_t)other) ||
                !qa_stamp_set_mark(&work->connected, (size_t)other)) continue;
            ++visited; work->pending[depth++] = other;
        }
    }
    return visited == count;
}
static void possible_portal(aas_clustering *work, int32_t root) {
    const qa_aas_setting *setting = work->settings + root;
    if ((setting->contents & AAS_CLUSTER_PORTAL) || !(setting->flags & AAS_GROUNDED)) return;
    size_t candidates = adjacent_candidates(work, root);
    qa_stamp_set_begin(&work->front_members);
    qa_stamp_set_begin(&work->back_members);
    size_t front_count = 0, back_count = 0, front_faces = 0, back_faces = 0;
    int32_t front_plane = -1, back_plane = -1;
    for (size_t i = 0; i < candidates; ++i) {
        int32_t current = work->candidate_areas[i];
        const qa_aas_area *area = work->source->areas + current;
        bool has_front = false, has_back = false;
        for (int32_t j = 0; j < area->face_count; ++j) {
            int32_t number;
            const qa_aas_face *face = area_face(work, area, j, &number);
            if (face->flags & AAS_FACE_SOLID) continue;
            int32_t other = other_area(face, current);
            if (other != current && qa_stamp_set_test(&work->candidates, (size_t)other)) continue;
            if (work->settings[other].contents & AAS_CLUSTER_PORTAL) return;
            int32_t plane = face->plane & ~1;
            if (front_plane < 0 || plane == front_plane) {
                front_plane = plane; has_front = true;
                work->front_faces[front_faces++] = number;
                if (qa_stamp_set_mark(&work->front_members, (size_t)other))
                    work->front_areas[front_count++] = other;
            } else if (back_plane < 0 || plane == back_plane) {
                back_plane = plane; has_back = true;
                work->back_faces[back_faces++] = number;
                if (qa_stamp_set_mark(&work->back_members, (size_t)other))
                    work->back_areas[back_count++] = other;
            } else return;
        }
        if (!has_front || !has_back) return;
    }
    if (!connected(work, work->front_areas, front_count, &work->front_members) ||
        !connected(work, work->back_areas, back_count, &work->back_members)) return;
    qa_stamp_set_begin(&work->front_edges);
    for (size_t i = 0; i < front_faces; ++i) {
        const qa_aas_face *face = work->source->faces + work->front_faces[i];
        for (int32_t j = 0; j < face->edge_count; ++j)
            qa_stamp_set_mark(&work->front_edges,
                nav_aas_oriented_index(work->source->edge_index[face->first_edge + j]));
    }
    for (size_t i = 0; i < back_faces; ++i) {
        const qa_aas_face *face = work->source->faces + work->back_faces[i];
        for (int32_t j = 0; j < face->edge_count; ++j)
            if (qa_stamp_set_test(&work->front_edges,
                nav_aas_oriented_index(work->source->edge_index[face->first_edge + j]))) return;
    }
    for (size_t i = 0; i < candidates; ++i)
        work->settings[work->candidate_areas[i]].contents |= AAS_CLUSTER_PORTAL | AAS_ROUTE_PORTAL;
}
static void create_portals(aas_clustering *work) {
    work->portal_count = work->cluster_count = 1; work->index_count = 0; work->retry = false;
    work->portals[0] = (qa_aas_portal){0}; work->clusters[0] = (qa_aas_cluster){0};
    size_t areas = work->source->count[QA_AAS_AREAS];
    for (size_t i = 0; i < areas; ++i) {
        work->portal_for_area[i] = 0;
        if (!i || !(work->settings[i].contents & AAS_CLUSTER_PORTAL)) continue;
        size_t number = work->portal_count++;
        work->portal_for_area[i] = (int32_t)number;
        work->portals[number] = (qa_aas_portal){.area = (int32_t)i};
    }
}
static bool update_portal(aas_clustering *work, int32_t area, int32_t cluster) {
    int32_t number = work->portal_for_area[area];
    qa_aas_portal *portal = work->portals + number;
    if (portal->front_cluster == cluster || portal->back_cluster == cluster) return true;
    if (!portal->front_cluster) portal->front_cluster = cluster;
    else if (!portal->back_cluster) portal->back_cluster = cluster;
    else {
        work->settings[area].contents &= ~AAS_CLUSTER_PORTAL;
        work->retry = true; return false;
    }
    work->settings[area].cluster = -number;
    qa_aas_cluster *record = work->clusters + cluster;
    work->portal_index[record->first_portal + record->portal_count++] = number;
    ++work->index_count; return true;
}
static bool enter_area(aas_clustering *work, int32_t area, int32_t cluster,
    size_t *depth, qa_error *error) {
    qa_aas_setting *setting = work->settings + area;
    if (setting->cluster > 0) {
        if (setting->cluster == cluster) return true;
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)area, "AAS reachability joins previously separated clusters");
        return false;
    }
    if (setting->contents & AAS_CLUSTER_PORTAL) return update_portal(work, area, cluster);
    setting->cluster = cluster;
    setting->cluster_area = work->clusters[cluster].area_count++;
    work->stack[(*depth)++] = (aas_cluster_visit){.area = area};
    return true;
}
static bool flood(aas_clustering *work, int32_t root, int32_t cluster, qa_error *error) {
    size_t depth = 0;
    if (!enter_area(work, root, cluster, &depth, error)) return false;
    while (depth) {
        aas_cluster_visit *visit = work->stack + depth - 1;
        const qa_aas_setting *setting = work->settings + visit->area;
        if (visit->next == setting->reach_count) { --depth; continue; }
        int32_t other = work->source->reachability[setting->first_reach + visit->next++].area;
        if (other && !enter_area(work, other, cluster, &depth, error)) return false;
    }
    return true;
}
static bool flood_incoming(aas_clustering *work, int32_t cluster, qa_error *error) {
    for (size_t i = 1; i < work->source->count[QA_AAS_AREAS]; ++i) {
        const qa_aas_setting *setting = work->settings + i;
        if (setting->cluster || (setting->contents & AAS_CLUSTER_PORTAL)) continue;
        for (int32_t j = 0; j < setting->reach_count; ++j) {
            int32_t other = work->source->reachability[setting->first_reach + j].area;
            const qa_aas_setting *target = work->settings + other;
            if ((target->contents & AAS_CLUSTER_PORTAL) || !target->cluster) continue;
            if (!flood(work, (int32_t)i, cluster, error)) return false;
            i = 0; break;
        }
    }
    return true;
}
static void number_cluster(aas_clustering *work, int32_t number) {
    qa_aas_cluster *cluster = work->clusters + number;
    cluster->area_count = cluster->reachable_area_count = 0;
    for (unsigned reachable = 1; reachable <= 2; ++reachable) {
        bool with_reach = reachable == 1;
        for (size_t area = 1; area < work->source->count[QA_AAS_AREAS]; ++area) {
            qa_aas_setting *setting = work->settings + area;
            if (setting->cluster != number || (setting->reach_count != 0) != with_reach) continue;
            setting->cluster_area = cluster->area_count++;
            if (with_reach) ++cluster->reachable_area_count;
        }
        for (int32_t i = 0; i < cluster->portal_count; ++i) {
            qa_aas_portal *portal = work->portals + work->portal_index[cluster->first_portal + i];
            if ((work->settings[portal->area].reach_count != 0) != with_reach) continue;
            portal->cluster_areas[portal->front_cluster == number ? 0 : 1] = cluster->area_count++;
            if (with_reach) ++cluster->reachable_area_count;
        }
    }
}
static bool find_clusters(aas_clustering *work, qa_error *error) {
    size_t areas = work->source->count[QA_AAS_AREAS];
    for (size_t i = 1; i < areas; ++i) work->settings[i].cluster = 0;
    for (size_t i = 1; i < areas; ++i) {
        const qa_aas_setting *setting = work->settings + i;
        if (setting->cluster || !setting->reach_count || (setting->contents & AAS_CLUSTER_PORTAL)) continue;
        int32_t number = (int32_t)work->cluster_count;
        work->clusters[number] = (qa_aas_cluster){.first_portal = (int32_t)work->index_count};
        if (!flood(work, (int32_t)i, number, error) || !flood_incoming(work, number, error)) return false;
        number_cluster(work, number); ++work->cluster_count;
    }
    return true;
}
static bool test_portals(aas_clustering *work) {
    for (size_t i = 1; i < work->portal_count; ++i) {
        const qa_aas_portal *portal = work->portals + i;
        if (portal->front_cluster && portal->back_cluster) continue;
        work->settings[portal->area].contents &= ~AAS_CLUSTER_PORTAL;
        work->retry = true; return false;
    }
    return true;
}
bool qa_aas_cluster_rebuild(const qa_aas_view *source, qa_nav_asset **out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing clustered AAS output");
        return false;
    }
    if (!qa_aas_validate(source, error)) return false;
    aas_clustering work = {.source = source};
    size_t counts[QA_AAS_LUMP_COUNT]; memcpy(counts, source->count, sizeof(counts));
    qa_nav_asset *asset = NULL;
    if (counts[QA_AAS_AREAS]) {
        if (!prepare(&work, error)) goto failed;
        for (size_t i = 1; i < counts[QA_AAS_AREAS]; ++i)
            if (work.settings[i].contents & AAS_VIEW_PORTAL) work.settings[i].contents |= AAS_CLUSTER_PORTAL;
        for (size_t i = 1; i < counts[QA_AAS_AREAS]; ++i) possible_portal(&work, (int32_t)i);
        for (size_t i = 1; i < counts[QA_AAS_AREAS]; ++i)
            if (work.settings[i].contents & AAS_CLUSTER_PORTAL) work.settings[i].contents |= AAS_VIEW_PORTAL;
        for (;;) {
            create_portals(&work);
            if (!find_clusters(&work, error)) {
                if (work.retry) continue;
                goto failed;
            }
            if (!test_portals(&work)) continue;
            break;
        }
        counts[QA_AAS_PORTALS] = work.portal_count;
        counts[QA_AAS_PORTAL_INDEX] = work.index_count;
        counts[QA_AAS_CLUSTERS] = work.cluster_count;
    }
    if (!nav_aas_allocate(counts, &asset, error)) goto failed;
    uint32_t changed = (UINT32_C(1) << QA_AAS_SETTINGS) | (UINT32_C(1) << QA_AAS_PORTALS) |
        (UINT32_C(1) << QA_AAS_PORTAL_INDEX) | (UINT32_C(1) << QA_AAS_CLUSTERS);
    nav_aas_copy_records(source, &asset->aas, work.settings ? changed : 0);
    if (work.settings) {
        memcpy((void *)asset->aas.settings, work.settings, counts[QA_AAS_SETTINGS] * sizeof(*work.settings));
        memcpy((void *)asset->aas.portals, work.portals, work.portal_count * sizeof(*work.portals));
        memcpy((void *)asset->aas.portal_index, work.portal_index, work.index_count * sizeof(*work.portal_index));
        memcpy((void *)asset->aas.clusters, work.clusters, work.cluster_count * sizeof(*work.clusters));
    }
    if (!qa_aas_validate(&asset->aas, error)) goto failed;
    qa_arena_destroy(&work.storage); *out = asset; return true;
failed:
    qa_nav_asset_release(asset); qa_arena_destroy(&work.storage); return false;
}
