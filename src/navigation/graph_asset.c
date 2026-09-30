#include "internal.h"

static qa_nav_origin origin_kind(qa_nav_origin_kind kind, uint32_t node, uint32_t link) {
    return (qa_nav_origin){.kind = kind,
                           .node = node,
                           .link = link,
                           .surface = QA_NAV_NO_INDEX,
                           .leaf = QA_NAV_NO_INDEX};
}
qa_nav_edge nav_asset_aas_edge(const qa_aas_view *asset,uint32_t from,uint32_t index) {
    const qa_aas_reach *reach=asset->reachability+index;
    qa_nav_travel mode=qa_nav_aas_travel((uint32_t)reach->travel_type);
    qa_nav_edge edge={.id=index,.from=from,.to=(uint32_t)reach->area,.mode=mode,
        .start=reach->start,.end=reach->end,.travel_seconds=fmaxf(1,reach->travel_time)/100,
        .source_travel_type=(uint32_t)reach->travel_type,
        .source=origin_kind(QA_NAV_ORIGIN_AAS,from,index),
        .has_entity=mode==QA_NAV_MOVER || mode==QA_NAV_TELEPORT || mode==QA_NAV_JUMP_PAD};
    if(edge.has_entity) edge.entity=(qa_nav_binding){.has_model=mode==QA_NAV_MOVER,
        .model=reach->face & 65535,.bounds=asset->areas[from].bounds,
        .raw={reach->face,reach->edge},.raw_count=2};
    return edge;
}
qa_nav_edge nav_asset_kex_edge(const qa_nav_graph *g,const qa_nav_source_view *asset,uint32_t from,
                              uint32_t index,const qa_nav_source_entity *binding) {
    const qa_nav_source_link *link=asset->links+index;
    qa_nav_travel mode=qa_nav_kex_travel(link->type);
    qa_nav_origin_kind kind=asset->kind==QA_NAV_NAV2?QA_NAV_ORIGIN_NAV2:QA_NAV_ORIGIN_NAV3;
    qa_vec3 rise=qa_v3(0,0,-g->view.profile.shape.bounds.mins.z);
    qa_nav_edge edge={.id=index,.from=from,.to=link->target,.mode=mode,
        .start=g->nodes[from].origin,.end=g->nodes[link->target].origin,
        .source_travel_type=link->type,.source_flags=link->flags,
        .source=origin_kind(kind,from,index),.has_hint=link->traversal!=UINT16_MAX};
    if(edge.has_hint) {
        edge.hint=asset->traversals[link->traversal];
        edge.hint.funnel=qa_vec_add(edge.hint.funnel,rise);
        edge.hint.start=qa_vec_add(edge.hint.start,rise);
        edge.hint.end=qa_vec_add(edge.hint.end,rise);
        edge.start=edge.hint.start;edge.end=edge.hint.end;
    }
    edge.travel_seconds=mode==QA_NAV_TELEPORT?0.01f:
        fmaxf(0.01f,nav_distance(edge.start,edge.end)*asset->heuristic/320);
    edge.has_entity=binding!=NULL || mode==QA_NAV_TELEPORT || mode==QA_NAV_JUMP_PAD || mode==QA_NAV_MOVER;
    if(binding) {
        edge.entity=(qa_nav_binding){.has_model=binding->has_model,.model=binding->model,.bounds=binding->bounds,
            .raw={binding->tail[0],binding->tail[1]},.raw_count=binding->tail_count};
        if(asset->kind==QA_NAV_NAV3) {
            if(binding->model<=1 || binding->model==255) edge.entity.has_model=false;
            else edge.entity.model-=binding->model>255?2:1;
        }
    } else if(edge.has_entity) edge.entity.bounds=g->nodes[from].bounds;
    return edge;
}
static bool from_aas(qa_nav_graph *g, const qa_aas_view *asset, const qa_navigation_services *s,
                     qa_error *e) {
    const qa_nav_profile *p = &g->view.profile;
    for (size_t i = 1; i < asset->count[QA_AAS_AREAS]; ++i) {
        const qa_aas_area *area = asset->areas + i;
        const qa_aas_setting *setting = asset->settings + i;
        qa_nav_node node = {.id = (uint32_t)i,
                            .origin = area->center,
                            .bounds = area->bounds,
                            .flags = (uint32_t)setting->flags,
                            .presence = (uint32_t)setting->presence,
                            .source_cluster = setting->cluster,
                            .source = origin_kind(QA_NAV_ORIGIN_AAS, (uint32_t)i, QA_NAV_NO_INDEX)};
        qa_nav_profile posture = *p;
        if ((setting->presence & 2) == 0 && (setting->presence & 4) != 0)
            (void)nav_crouch_profile(p, &posture);
        if ((setting->flags & 1) != 0) {
            qa_vec3 end = area->center;
            end.z = area->bounds.mins.z + posture.shape.bounds.mins.z - p->maximum_step;
            qa_trace_result floor;
            if (!nav_trace(s, &posture, (qa_actor_id){0}, area->center, end, false, &floor, e))
                return false;
            if (!floor.start_solid && !floor.all_solid && floor.fraction < 1 && floor.contact &&
                floor.contact_plane.normal.z >= p->minimum_floor_normal)
                node.origin = floor.end;
            else
                for (int32_t j = 0; j < setting->reach_count; ++j) {
                    const qa_aas_reach *reach = asset->reachability + setting->first_reach + j;
                    bool clear;
                    if (!nav_clear(s, &posture, (qa_actor_id){0}, reach->start, reach->start, false,
                                   &clear, e))
                        return false;
                    if (clear) {
                        node.origin = reach->start;
                        break;
                    }
                }
        }
        if (!nav_contents(s, p, (qa_actor_id){0}, node.origin, false, &node.contents, e) ||
            !nav_graph_node(g, &node, e))
            return false;
        for (int32_t j = 0; j < setting->reach_count; ++j) {
            uint32_t index = (uint32_t)setting->first_reach + (uint32_t)j;
            const qa_aas_reach *reach = asset->reachability + index;
            if (reach->area == 0)
                continue;
            qa_nav_edge edge=nav_asset_aas_edge(asset,(uint32_t)i,index);
            if (!nav_graph_edge(g, &edge, e))
                return false;
        }
    }
    return true;
}
static bool from_kex(qa_nav_graph *g, const qa_nav_source_view *asset,
                     const qa_navigation_services *s, qa_error *e) {
    const qa_nav_profile *p = &g->view.profile;
    qa_vec3 rise = qa_v3(0, 0, -p->shape.bounds.mins.z);
    qa_nav_origin_kind kind = asset->kind == QA_NAV_NAV2 ? QA_NAV_ORIGIN_NAV2 : QA_NAV_ORIGIN_NAV3;
    for (size_t i = 0; i < asset->node_count; ++i) {
        const qa_nav_source_node *source = asset->nodes + i;
        qa_nav_node node = {.id = (uint32_t)i,
                            .origin = qa_vec_add(source->origin, rise),
                            .radius = source->radius,
                            .flags = source->flags,
                            .source_cluster = -1,
                            .source = origin_kind(kind, (uint32_t)i, QA_NAV_NO_INDEX)};
        node.bounds = qa_bounds_translate(p->shape.bounds, node.origin);
        if (!nav_contents(s, p, (qa_actor_id){0}, node.origin, false, &node.contents, e) ||
            !nav_graph_node(g, &node, e))
            return false;
    }
    const qa_nav_source_entity **bindings =
        calloc(asset->link_count == 0 ? 1 : asset->link_count, sizeof(*bindings));
    if (bindings == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Indexing navigation entity bindings");
        return false;
    }
    for (size_t i = 0; i < asset->entity_count; ++i)
        bindings[asset->entities[i].link] = asset->entities + i;
    bool ok = true;
    for (size_t i = 0; ok && i < asset->node_count; ++i) {
        const qa_nav_source_node *source = asset->nodes + i;
        for (uint32_t j = 0; ok && j < source->link_count; ++j) {
            uint32_t index = source->first_link + j;
            const qa_nav_source_entity *binding = bindings[index];
            qa_nav_edge edge=nav_asset_kex_edge(g,asset,(uint32_t)i,index,binding);
            ok = nav_graph_edge(g, &edge, e);
        }
    }
    free(bindings);
    return ok;
}
bool qa_nav_graph_from_asset(const qa_nav_map *map, qa_nav_asset *asset,
                             const qa_nav_profile *profile, const qa_navigation_services *services,
                             qa_nav_graph **out, qa_error *e) {
    if (asset == NULL || out == NULL || !nav_services_valid(services, false, e)) {
        if (asset == NULL || out == NULL)
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation graph asset/output");
        return false;
    }
    qa_nav_graph *g;
    if (!nav_graph_new(map, profile, &g, e))
        return false;
    qa_nav_asset_retain(asset);
    g->view.asset = asset;
    bool ok = asset->kind == QA_NAV_AAS ? from_aas(g, &asset->aas, services, e)
                                        : from_kex(g, &asset->kex, services, e);
    if (!ok || !nav_graph_finish(g, e)) {
        qa_nav_graph_release(g);
        return false;
    }
    *out = g;
    return true;
}
