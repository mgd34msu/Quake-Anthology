#include "asset_internal.h"

/* AAS_Optimize, be_aas_optimize.c: retain ladder geometry after reachability. */
typedef struct aas_optimization {
    const qa_aas_view *source;
    qa_aas_view *output;
    int32_t *vertices, *edges, *faces;
    size_t vertex_count, edge_count, face_count, edge_indexes, face_indexes;
} aas_optimization;

static int32_t oriented(int32_t value,int32_t mapped) {
    return value<0?-mapped:mapped;
}
static bool ordinary_reach(const qa_aas_reach *reach) {
    uint32_t travel=(uint32_t)reach->travel_type&UINT32_C(0xffffff);
    return travel!=11 && travel!=18 && travel!=19;
}
static bool reach_indices(const qa_aas_view *view,qa_error *error) {
    for(size_t i=0;i<view->count[QA_AAS_REACHABILITY];++i) {
        const qa_aas_reach *reach=view->reachability+i;
        if(ordinary_reach(reach) &&
           ((reach->face && nav_aas_oriented_index(reach->face)>=view->count[QA_AAS_FACES]) ||
            (reach->edge && nav_aas_oriented_index(reach->edge)>=view->count[QA_AAS_EDGES]))) {
            qa_error_set(error,QA_ERROR_FORMAT,i,"Invalid AAS reachability geometry index");
            return false;
        }
    }
    if(!view->count[QA_AAS_VERTICES])
        for(size_t i=0;i<view->count[QA_AAS_FACES];++i)
            if((view->faces[i].flags&2) && view->faces[i].edge_count) {
                qa_error_set(error,QA_ERROR_FORMAT,i,"AAS ladder edge has no vertices");
                return false;
            }
    return true;
}
static int32_t vertex(aas_optimization *work,int32_t number) {
    int32_t mapped=work->vertices[number];
    if(mapped) return mapped;
    mapped=(int32_t)work->vertex_count++;
    work->vertices[number]=mapped;
    if(work->output) ((qa_vec3 *)work->output->vertices)[mapped]=work->source->vertices[number];
    return mapped;
}
static int32_t edge(aas_optimization *work,int32_t number) {
    size_t index=nav_aas_oriented_index(number);
    int32_t mapped=work->edges[index];
    if(!mapped) {
        mapped=(int32_t)work->edge_count++;
        const qa_aas_edge *source=work->source->edges+index;
        qa_aas_edge value={{vertex(work,source->vertices[0]),vertex(work,source->vertices[1])}};
        work->edges[index]=mapped;
        if(work->output) ((qa_aas_edge *)work->output->edges)[mapped]=value;
    }
    return number>0?mapped:-mapped;
}
static int32_t face(aas_optimization *work,int32_t number) {
    size_t index=nav_aas_oriented_index(number);
    const qa_aas_face *source=work->source->faces+index;
    if(!(source->flags&2)) return 0;
    int32_t mapped=work->faces[index];
    if(!mapped) {
        mapped=(int32_t)work->face_count++;
        qa_aas_face value=*source;value.first_edge=(int32_t)work->edge_indexes;
        for(int32_t i=0;i<source->edge_count;++i) {
            int32_t mapped_edge=edge(work,work->source->edge_index[source->first_edge+i]);
            if(work->output) ((int32_t *)work->output->edge_index)[work->edge_indexes]=mapped_edge;
            ++work->edge_indexes;
        }
        work->faces[index]=mapped;
        if(work->output) ((qa_aas_face *)work->output->faces)[mapped]=value;
    }
    return number>0?mapped:-mapped;
}
static bool geometry(aas_optimization *work,qa_error *error) {
    const qa_aas_view *view=work->source;
    work->vertex_count=0;work->edge_count=1;work->face_count=1;
    work->edge_indexes=0;work->face_indexes=0;
    memset(work->vertices,0,view->count[QA_AAS_VERTICES]*sizeof(*work->vertices));
    memset(work->edges,0,view->count[QA_AAS_EDGES]*sizeof(*work->edges));
    memset(work->faces,0,view->count[QA_AAS_FACES]*sizeof(*work->faces));
    for(size_t i=1;i<view->count[QA_AAS_AREAS];++i) {
        const qa_aas_area *source=view->areas+i;
        qa_aas_area value=*source;value.first_face=(int32_t)work->face_indexes;value.face_count=0;
        for(int32_t j=0;j<source->face_count;++j) {
            int32_t mapped=face(work,view->face_index[source->first_face+j]);
            if(!mapped) continue;
            if(work->output) ((int32_t *)work->output->face_index)[work->face_indexes]=mapped;
            ++work->face_indexes;++value.face_count;
        }
        if(work->output) ((qa_aas_area *)work->output->areas)[i]=value;
    }
    if(work->vertex_count>INT32_MAX || work->edge_count>INT32_MAX || work->face_count>INT32_MAX ||
       work->edge_indexes>INT32_MAX || work->face_indexes>INT32_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Optimized AAS geometry exceeds source index range");
        return false;
    }
    return true;
}
bool qa_aas_optimize(const qa_aas_view *source,qa_nav_asset **out,qa_error *error) {
    if(!out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing optimized AAS output");
        return false;
    }
    if(!qa_aas_validate(source,error) || !reach_indices(source,error)) return false;
    size_t counts[QA_AAS_LUMP_COUNT];memcpy(counts,source->count,sizeof(counts));
    if(!source->count[QA_AAS_FACES] || !source->count[QA_AAS_AREAS]) {
        qa_nav_asset *copy=NULL;
        if(!nav_aas_allocate(counts,&copy,error)) return false;
        nav_aas_copy_records(source,&copy->aas,0);*out=copy;return true;
    }
    qa_arena scratch={0};aas_optimization work={.source=source};
#define MAP(field,lump) do { \
    size_t count=source->count[lump]; \
    if(count>SIZE_MAX/sizeof(*work.field)) { \
        qa_error_set(error,QA_ERROR_MEMORY,lump,"AAS remapping workspace exceeds addressable storage"); \
        goto failed; \
    } \
    work.field=qa_arena_alloc(&scratch,(count?count:1)*sizeof(*work.field),_Alignof(int32_t),error); \
    if(!work.field) goto failed; \
} while(0)
    MAP(vertices,QA_AAS_VERTICES);MAP(edges,QA_AAS_EDGES);MAP(faces,QA_AAS_FACES);
#undef MAP
    if(!geometry(&work,error)) goto failed;
    counts[QA_AAS_VERTICES]=work.vertex_count;counts[QA_AAS_EDGES]=work.edge_count;
    counts[QA_AAS_FACES]=work.face_count;counts[QA_AAS_EDGE_INDEX]=work.edge_indexes;
    counts[QA_AAS_FACE_INDEX]=work.face_indexes;
    qa_nav_asset *asset=NULL;
    if(!nav_aas_allocate(counts,&asset,error)) goto failed;
    uint32_t compacted = (UINT32_C(1) << QA_AAS_VERTICES) | (UINT32_C(1) << QA_AAS_EDGES) |
        (UINT32_C(1) << QA_AAS_EDGE_INDEX) | (UINT32_C(1) << QA_AAS_FACES) |
        (UINT32_C(1) << QA_AAS_FACE_INDEX);
    nav_aas_copy_records(source,&asset->aas,compacted);work.output=&asset->aas;
    if(!geometry(&work,error)) {qa_nav_asset_release(asset);goto failed;}
    if(source->count[QA_AAS_AREAS]) {
        qa_aas_area *zero=(qa_aas_area *)asset->aas.areas;
        zero->first_face=0;zero->face_count=0;
    }
    qa_aas_reach *reaches=(qa_aas_reach *)asset->aas.reachability;
    for(size_t i=0;i<counts[QA_AAS_REACHABILITY];++i) if(ordinary_reach(reaches+i)) {
        reaches[i].face=source->reachability[i].face?
            oriented(reaches[i].face,work.faces[nav_aas_oriented_index(reaches[i].face)]):0;
        reaches[i].edge=source->reachability[i].edge?
            oriented(reaches[i].edge,work.edges[nav_aas_oriented_index(reaches[i].edge)]):0;
    }
    if(!qa_aas_validate(&asset->aas,error)) {qa_nav_asset_release(asset);goto failed;}
    qa_arena_destroy(&scratch);*out=asset;return true;
failed:
    qa_arena_destroy(&scratch);return false;
}
