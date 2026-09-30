#include "internal.h"
#include "qa/navigation_graph_save.h"
#include "qa/persistence_fields.h"

static bool fail(qa_source_save_io *io, const char *message) {
    if (!io->failed) qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    io->failed=true; return false;
}
static bool signature(qa_source_save_io *io) {
    static const uint8_t expected[8]={'Q','A','N','G','R','A','P',0};
    uint8_t magic[8]; memcpy(magic,expected,8); uint32_t version=1;
    return qa_source_save_bytes(io,magic,8) && qa_source_save_u32(io,&version) &&
        ((!memcmp(magic,expected,8) && version==1) || fail(io,"Unsupported immutable navigation graph"));
}
static bool shape(qa_source_save_io *io, qa_trace_shape *shape) {
    uint32_t kind=shape->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_SHAPE_CAPSULE ||
        !qa_persistence_bounds(io,&shape->bounds)) return fail(io,"Invalid navigation shape");
    shape->kind=(qa_shape_kind)kind; return true;
}
static bool profile(qa_source_save_io *io, qa_nav_profile *p) {
    uint32_t family=p->policy.family, q1_move=p->policy.q1_move;
    if (!qa_persistence_movement_profile(io,&p->movement) || !shape(io,&p->shape) ||
        !shape(io,&p->crouched_shape) || !qa_source_save_u32(io,&family) ||
        family<QA_COLLISION_Q1 || family>QA_COLLISION_Q3 || !qa_source_save_u32(io,&p->policy.contents_mask) ||
        !qa_source_save_u32(io,&q1_move) || q1_move>QA_Q1_MOVE_MISSILE ||
        !qa_source_save_i32(io,&p->policy.q1_hull) || !qa_source_save_bool(io,&p->policy.q2_merged_contents) ||
        !qa_source_save_bool(io,&p->policy.curves) ||
        !qa_source_save_bool(io,&p->policy.player_curve_clip) ||
        !qa_source_save_u32(io,&p->capabilities) || !qa_source_save_f32(io,&p->maximum_step) ||
        !qa_source_save_f32(io,&p->minimum_floor_normal) || !qa_source_save_f32(io,&p->maximum_drop) ||
        !qa_source_save_u8(io,&p->team) || !qa_source_save_bool(io,&p->monster) ||
        !qa_source_save_bool(io,&p->has_crouched_shape)) return false;
    p->policy.family=(qa_collision_family)family; p->policy.q1_move=(qa_q1_move_kind)q1_move;
    return nav_profile_valid(p,io->error);
}
static bool origin(qa_source_save_io *io, qa_nav_origin *o) {
    uint32_t kind=o->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_NAV_ORIGIN_CONSTRUCTED ||
        !qa_source_save_u32(io,&o->node) || !qa_source_save_u32(io,&o->link) ||
        !qa_source_save_u32(io,&o->surface) || !qa_source_save_u32(io,&o->leaf)) return false;
    o->kind=(qa_nav_origin_kind)kind; return true;
}
static bool node(qa_source_save_io *io, qa_nav_node *n) {
    return qa_source_save_u32(io,&n->id) && qa_source_save_vec3(io,&n->origin) &&
        qa_persistence_bounds(io,&n->bounds) && qa_source_save_f32(io,&n->radius) &&
        qa_source_save_u32(io,&n->contents) && qa_source_save_u32(io,&n->flags) &&
        qa_source_save_u32(io,&n->presence) && qa_source_save_i32(io,&n->source_cluster) && origin(io,&n->source);
}
static bool edge(qa_source_save_io *io, qa_nav_edge *e) {
    uint32_t mode=e->mode;
    if (!qa_source_save_u32(io,&e->id) || !qa_source_save_u32(io,&e->from) ||
        !qa_source_save_u32(io,&e->to) || !qa_source_save_u32(io,&mode) || mode>=QA_NAV_TRAVEL_COUNT ||
        !qa_source_save_vec3(io,&e->start) || !qa_source_save_vec3(io,&e->end) ||
        !qa_source_save_f32(io,&e->travel_seconds) || !qa_source_save_u32(io,&e->source_travel_type) ||
        !qa_source_save_u32(io,&e->source_flags) || !qa_source_save_bool(io,&e->has_hint) ||
        !qa_source_save_bool(io,&e->has_entity) || !qa_source_save_vec3(io,&e->hint.funnel) ||
        !qa_source_save_vec3(io,&e->hint.start) || !qa_source_save_vec3(io,&e->hint.end) ||
        !qa_source_save_vec3(io,&e->hint.ladder_plane) || !qa_source_save_bool(io,&e->hint.has_ladder_plane) ||
        !qa_source_save_bool(io,&e->entity.has_model) || !qa_source_save_i32(io,&e->entity.model) ||
        !qa_persistence_bounds(io,&e->entity.bounds) || !qa_source_save_i32(io,&e->entity.raw[0]) ||
        !qa_source_save_i32(io,&e->entity.raw[1]) || !qa_source_save_u8(io,&e->entity.raw_count) ||
        e->entity.raw_count>2 || !origin(io,&e->source)) return false;
    e->mode=(qa_nav_travel)mode; return true;
}
static bool same_edge(qa_source_save_io *io,qa_nav_edge actual,qa_nav_edge expected) {
    qa_source_save_io a={0},b={0};qa_buffer x={0},y={0};
    bool ok=qa_source_save_writer(&a,NULL,io->error) && edge(&a,&actual) && qa_source_save_finish(&a,&x) &&
        qa_source_save_writer(&b,NULL,io->error) && edge(&b,&expected) && qa_source_save_finish(&b,&y) &&
        x.size==y.size && !memcmp(x.data,y.data,x.size);
    qa_source_save_dispose(&a);qa_source_save_dispose(&b);qa_buffer_free(&x);qa_buffer_free(&y);
    return ok || fail(io,"Navigation edge differs from actual asset declaration");
}
static bool source_identity(qa_source_save_io *io, const qa_nav_graph *g) {
    const qa_aas_view *aas=qa_nav_asset_aas(g->view.asset);
    const qa_nav_source_view *kex=qa_nav_asset_kex(g->view.asset);
    qa_nav_origin_kind kind=aas?QA_NAV_ORIGIN_AAS:kex?
        (kex->kind==QA_NAV_NAV2?QA_NAV_ORIGIN_NAV2:QA_NAV_ORIGIN_NAV3):QA_NAV_ORIGIN_CONSTRUCTED;
    size_t nodes=aas?aas->count[QA_AAS_AREAS]:kex?kex->node_count:g->view.node_count;
    size_t edges=aas?aas->count[QA_AAS_REACHABILITY]:kex?kex->link_count:g->view.edge_count;
    size_t expected_edges=g->view.edge_count;
    if(aas) {
        expected_edges=0;
        for(size_t i=1;i<aas->count[QA_AAS_AREAS];++i) {
            const qa_aas_setting *s=aas->settings+i;
            for(int32_t j=0;j<s->reach_count;++j)
                if(aas->reachability[s->first_reach+j].area) ++expected_edges;
        }
    } else if(kex) {
        expected_edges=0;
        for(size_t i=0;i<kex->node_count;++i) expected_edges+=kex->nodes[i].link_count;
    }
    if (((aas || kex) && g->view.rejected_count) || (aas && g->view.node_count!=(nodes?nodes-1:0)) ||
        (kex && g->view.node_count!=nodes) || g->view.edge_count!=expected_edges)
        return fail(io,"Navigation graph omits actual asset topology");
    for (size_t i=0;i<g->view.node_count;++i) {
        const qa_nav_node *n=g->nodes+i;
        if (n->id>=nodes || n->source.kind!=kind ||
            (kind!=QA_NAV_ORIGIN_CONSTRUCTED && n->source.node!=n->id) ||
            (kind==QA_NAV_ORIGIN_CONSTRUCTED && n->id!=i))
            return fail(io,"Navigation node differs from actual source identity");
        if(aas && (n->id!=i+1 || n->flags!=(uint32_t)aas->settings[n->id].flags ||
            n->presence!=(uint32_t)aas->settings[n->id].presence ||
            n->source_cluster!=aas->settings[n->id].cluster ||
            memcmp(&n->bounds,&aas->areas[n->id].bounds,sizeof(n->bounds))))
            return fail(io,"Navigation node differs from admitted AAS area");
        if(kex && (n->id!=i || n->flags!=kex->nodes[n->id].flags ||
            n->radius!=kex->nodes[n->id].radius))
            return fail(io,"Navigation node differs from admitted native node");
        if(aas || kex) {
            if(n->source.link!=QA_NAV_NO_INDEX || n->source.surface!=QA_NAV_NO_INDEX || n->source.leaf!=QA_NAV_NO_INDEX)
                return fail(io,"Navigation node provenance differs from admitted asset");
        }
        if(kex) {
            qa_vec3 actual_origin=qa_vec_add(kex->nodes[n->id].origin,qa_v3(0,0,-g->view.profile.shape.bounds.mins.z));
            qa_bounds bounds=qa_bounds_translate(g->view.profile.shape.bounds,actual_origin);
            if(memcmp(&n->origin,&actual_origin,sizeof(actual_origin)) || memcmp(&n->bounds,&bounds,sizeof(bounds)) ||
                n->presence || n->source_cluster!=-1)
                return fail(io,"Native navigation node differs from actual hull admission");
        } else if(aas && n->radius) return fail(io,"AAS navigation radius differs from actual source");
    }
    for (size_t i=0;i<g->view.edge_count;++i) {
        const qa_nav_edge *e=g->edges+i;
        if (e->id>=edges || e->source.kind!=kind ||
            (kind!=QA_NAV_ORIGIN_CONSTRUCTED && (e->source.link!=e->id || e->source.node!=e->from)) ||
            (kind==QA_NAV_ORIGIN_CONSTRUCTED && e->id!=i))
            return fail(io,"Navigation edge differs from actual source identity");
        if (aas && (e->to!=(uint32_t)aas->reachability[e->id].area ||
            !e->from || e->from>=nodes ||
            e->id<(uint32_t)aas->settings[e->from].first_reach ||
            e->id-(uint32_t)aas->settings[e->from].first_reach>=(uint32_t)aas->settings[e->from].reach_count ||
            e->source_travel_type!=(uint32_t)aas->reachability[e->id].travel_type))
            return fail(io,"Navigation edge differs from admitted AAS reachability");
        if (kex && (e->from>=nodes || e->id<kex->nodes[e->from].first_link ||
            e->id-kex->nodes[e->from].first_link>=kex->nodes[e->from].link_count ||
            e->to!=kex->links[e->id].target || e->source_travel_type!=kex->links[e->id].type))
            return fail(io,"Navigation edge differs from admitted native link");
    }
    for(size_t i=0;aas && i<g->view.edge_count;++i)
        if(!same_edge(io,g->edges[i],nav_asset_aas_edge(aas,g->edges[i].from,g->edges[i].id))) return false;
    if(kex) {
        const qa_nav_source_entity **bindings=calloc(kex->link_count?kex->link_count:1,sizeof(*bindings));
        if(!bindings) {qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Qualifying native navigation bindings");return false;}
        for(size_t i=0;i<kex->entity_count;++i) bindings[kex->entities[i].link]=kex->entities+i;
        bool ok=true;
        for(size_t i=0;ok && i<g->view.edge_count;++i) {
            const qa_nav_edge *e=g->edges+i;
            ok=same_edge(io,*e,nav_asset_kex_edge(g,kex,e->from,e->id,bindings[e->id]));
        }
        free(bindings);if(!ok) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_nav_graph *g, const qa_nav_map *expected) {
    uint32_t format=g->view.map.format;
    bool asset=g->view.asset!=NULL; uint32_t asset_kind=asset?qa_nav_asset_type(g->view.asset):0;
    bool admitted=asset; uint32_t admitted_kind=asset_kind;
    if (!qa_source_save_string(io,&g->view.map.name) || !qa_source_save_u32(io,&format) ||
        !qa_source_save_bytes(io,g->view.map.digest,32) || !profile(io,&g->view.profile) ||
        !qa_source_save_bool(io,&asset) || !qa_source_save_u32(io,&asset_kind) ||
        asset!=admitted || asset_kind!=admitted_kind) return fail(io,"Navigation asset qualification differs");
    g->view.map.format=(qa_bsp_format)format;
    if (expected && (g->view.map.name!=expected->name || format!=(uint32_t)expected->format ||
        memcmp(g->view.map.digest,expected->digest,32))) return fail(io,"Navigation map qualification differs");
    size_t count=g->view.node_count;
    if (!qa_source_save_count(io,&count,UINT32_MAX-1u)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (count>(io->input.size-io->offset)/76) return fail(io,"Truncated navigation nodes");
        for (size_t i=0;i<count;++i) {
            qa_nav_node value={0};
            if (!node(io,&value) || !nav_graph_node(g,&value,io->error)) return false;
        }
    } else for (size_t i=0;i<count;++i) if (!node(io,g->nodes+i)) return false;
    count=g->view.edge_count;
    if (!qa_source_save_count(io,&count,UINT32_MAX-1u)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (count>(io->input.size-io->offset)/153) return fail(io,"Truncated navigation edges");
        for (size_t i=0;i<count;++i) {
            qa_nav_edge value={0};
            if (!edge(io,&value) || !nav_graph_edge(g,&value,io->error)) return false;
        }
    } else for (size_t i=0;i<count;++i) if (!edge(io,g->edges+i)) return false;
    count=g->view.rejected_count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(*g->rejected))) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && count) {
        if (count>(io->input.size-io->offset)/6) return fail(io,"Truncated navigation rejections");
        g->rejected=calloc(count,sizeof(*g->rejected));
        if (!g->rejected) {qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Restoring navigation rejections");return false;}
        g->view.rejected=g->rejected; g->view.rejected_count=count;
    }
    for (size_t i=0;i<count;++i) if (!qa_source_save_u32(io,&g->rejected[i].connection) ||
        !qa_source_save_bool(io,&g->rejected[i].missing_start) ||
        !qa_source_save_bool(io,&g->rejected[i].missing_end)) return false;
    return source_identity(io,g);
}
bool qa_navigation_graph_save_capture(qa_session *session,const qa_nav_graph *graph,
                                      qa_buffer *out,qa_error *error) {
    if (!session || !graph || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing immutable graph capture owner");return false;}
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,session,error) && signature(&io) &&
        fields(&io,(qa_nav_graph *)graph,NULL) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_navigation_graph_save_restore(qa_session *session,qa_bytes bytes,const qa_nav_map *map,
                                      qa_nav_asset *asset,qa_nav_graph **out,qa_error *error) {
    if (!session || !map || !out || *out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing qualified empty graph candidate");return false;}
    qa_nav_graph *g=calloc(1,sizeof(*g));
    if (!g) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring immutable navigation graph");return false;}
    atomic_init(&g->references,1); qa_nav_asset_retain(asset);g->view.asset=asset;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,session,bytes,error) && signature(&io) && fields(&io,g,map) &&
        qa_source_save_finish(&io,NULL) && nav_graph_finish(g,error);
    qa_source_save_dispose(&io);
    if (!ok) {qa_nav_graph_release(g);return false;}
    *out=g;return true;
}
