#include "internal.h"
#include "q1/geometry.h"
#include <limits.h>
#include <stdalign.h>
#include <stdlib.h>

/* Negative children index this table, rather than retaining native tokens. */
#define Q1_EMPTY_REFERENCE (-1)
#define Q1_SOLID_REFERENCE (-2)
#define Q1_NAMED_TERMINALS 19u
#define Q1_CACHE_ENTRIES 128u
#define Q1_CACHE_CELLS 1024u
#define Q1_CACHE_FACES 4096u
#define Q1_CACHE_VERTICES 16384u

static const qa_bounds hull_bounds[3]={
    {{0,0,0},{0,0,0}},{{-16,-16,-24},{16,16,32}},{{-32,-32,-24},{32,32,64}}
};
static const q1v cell_axes[3]={{1,0,0},{0,1,0},{0,0,1}};
static const qa_collision_bits sky_surface_flags={UINT64_C(1)<<QA_SURFACE_SKY_NOIMPACT,
    UINT64_C(1)<<(QA_SURFACE_Q1_EXTENSION_2-64)};
typedef struct q1node { uint32_t plane; int32_t children[2]; } q1node;
typedef struct q1hull {
    const q1node *nodes; size_t count;
    const qa_collision_plane *planes; size_t plane_count;
    const qa_collision_terminal *terminals;
    int32_t root;
} q1hull;
typedef struct q1contactface {
    qa_bsp_face face;
    qa_collision_plane plane;
    qa_collision_surface surface;
    size_t next;
    bool has_surface;
} q1contactface;
typedef struct q1model {
    qa_bsp_model source; int32_t drawing_root,clip_roots[2];
    qa_bsp_range brushes;
    bool authored_brushes;
    q1contactface *contact_faces; size_t *face_buckets; size_t bucket_count;
} q1model;
typedef struct q1clip_cache {
    struct q1clip_cache *previous,*next;
    size_t model,face_count,vertex_count;
    q1bounds envelope;
    q1cells cells;
} q1clip_cache;
typedef struct q1state {
    qa_bsp_view bsp;
    qa_arena retained;
    const qa_collision_plane *planes; size_t plane_count;
    q1node *drawing,*clip; size_t drawing_count,clip_count;
    int32_t *leaf_references; size_t leaf_count;
    qa_collision_terminal *terminals,*brush_contents; size_t terminal_count;
    q1model *models; size_t model_count;
    size_t brush_count, brush_plane_count;
    q1cell **brush_cells;
} q1state;
typedef struct native_trace {
    float fraction; qa_vec3 end;
    bool start_solid,all_solid,in_open,in_water;
    qa_collision_plane plane; qa_collision_terminal contents;
} native_trace;

static bool blocks(qa_collision_bits contents,const qa_trace_policy *policy) {
    return qa_collision_bits_overlap(contents,policy==NULL?qa_collision_bit(QA_CONTENT_SOLID):policy->contents_mask);
}
static qa_collision_terminal hull_terminal(const q1hull *hull,int32_t reference) {
    return hull->terminals[(size_t)(-1-(int64_t)reference)];
}
static bool hull_node(const q1hull *hull,int32_t index,const q1node **out,qa_error *error) {
    if(index<hull->root || index<0 || (size_t)index>=hull->count) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid Quake hull node %d",index); return false;
    }
    const q1node *node=&hull->nodes[index];
    if(node->plane>=hull->plane_count) { qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid Quake hull plane"); return false; }
    *out=node; return true;
}
static float plane_distance(qa_collision_plane plane,qa_vec3 p) {
    if(plane.type>=0 && plane.type<3) return qa_vec_component(p,(unsigned)plane.type)-plane.distance;
    return qa_vec_dot(p,plane.normal)-plane.distance;
}
static bool hull_contents(const q1hull *hull,qa_vec3 point,int32_t index,qa_collision_terminal *out,qa_error *error) {
    size_t visits=0;
    while(index>=0) {
        if(visits++>=hull->count) { qa_error_set(error,QA_ERROR_FORMAT,0,"Cycle in Quake collision hull"); return false; }
        const q1node *node;
        if(!hull_node(hull,index,&node,error)) return false;
        index=node->children[plane_distance(hull->planes[node->plane],point)<0?1:0];
    }
    *out=hull_terminal(hull,index); return true;
}
typedef struct hull_frame {
    int32_t node,far_node; size_t depth;
    float p1f,p2f,t1,fraction,midf;
    qa_vec3 p1,p2,mid;
    uint32_t plane; bool awaiting_near;
} hull_frame;
typedef struct q1scratch {
    qa_arena scratch,cache_storage[2];
    hull_frame *frames;
    size_t frame_capacity;
    q1clip_cache entries[Q1_CACHE_ENTRIES],*cache_first,*cache_last;
    size_t cache_count,cache_cells,cache_faces,cache_vertices;
    unsigned active_cache;
} q1scratch;
/* An explicit continuation stack preserves RecursiveHullCheck's ordering and
 * backs out by the source 0.1 fraction, without trusting BSP depth to C's stack. */
static bool trace_hull(hull_frame *stack,const q1hull *hull,qa_vec3 start,qa_vec3 end,const qa_trace_policy *policy,native_trace *out,qa_error *error) {
    native_trace trace={1,end,false,true,false,false,{{0,0,0},0,0,0},{{0},0}};
    float epsilon=qa_collision_rules(policy).brush.epsilon;
    if(hull->count==SIZE_MAX) { qa_error_set(error,QA_ERROR_MEMORY,0,"Quake hull depth overflow"); return false; }
    size_t count=1;
    stack[0]=(hull_frame){.node=hull->root,.p1f=0,.p2f=1,.p1=start,.p2=end};
    while(count>0) {
        hull_frame *frame=&stack[count-1];
        if(!frame->awaiting_near) {
            if(frame->depth>hull->count) { qa_error_set(error,QA_ERROR_FORMAT,0,"Cycle in Quake trace hull"); return false; }
            if(frame->node<0) {
                qa_collision_terminal terminal=hull_terminal(hull,frame->node);
                if(blocks(terminal.bits,policy)) { trace.start_solid=true; trace.contents=terminal; }
                else { trace.all_solid=false; if(qa_collision_bits_equal(terminal.bits,(qa_collision_bits){0})) trace.in_open=true; else trace.in_water=true; }
                count--; continue;
            }
            const q1node *node;
            if(!hull_node(hull,frame->node,&node,error)) return false;
            qa_collision_plane plane=hull->planes[node->plane];
            float t1=plane_distance(plane,frame->p1),t2=plane_distance(plane,frame->p2);
            if((t1>=0 && t2>=0)||(t1<0 && t2<0)) {
                frame->node=node->children[t1<0?1:0]; frame->depth++; continue;
            }
            float fraction=qa_collision_clamp_fraction((t1+(t1<0?epsilon:-epsilon))/(t1-t2));
            frame->fraction=fraction; frame->t1=t1; frame->plane=node->plane;
            frame->midf=frame->p1f+(frame->p2f-frame->p1f)*fraction;
            frame->mid=qa_vec_lerp(frame->p1,frame->p2,fraction);
            frame->far_node=node->children[t1<0?0:1]; frame->awaiting_near=true;
            if(count>=hull->count+1) { qa_error_set(error,QA_ERROR_FORMAT,0,"Cycle in Quake trace hull"); return false; }
            stack[count++]=(hull_frame){.node=node->children[t1<0?1:0],.depth=frame->depth+1,
                .p1f=frame->p1f,.p2f=frame->midf,.p1=frame->p1,.p2=frame->mid};
            continue;
        }
        qa_collision_terminal far_contents;
        if(!hull_contents(hull,frame->mid,frame->far_node,&far_contents,error)) return false;
        if(!blocks(far_contents.bits,policy)) {
            frame->node=frame->far_node; frame->p1f=frame->midf; frame->p1=frame->mid;
            frame->depth++; frame->awaiting_near=false; continue;
        }
        if(trace.all_solid) { *out=trace; return true; }
        trace.plane=hull->planes[frame->plane];
        if(frame->t1<0) {
            trace.plane.normal=qa_vec_scale(trace.plane.normal,-1); trace.plane.distance=-trace.plane.distance;
            trace.plane.signbits=(uint8_t)((trace.plane.normal.x<0?1:0)|(trace.plane.normal.y<0?2:0)|(trace.plane.normal.z<0?4:0));
        }
        trace.contents=far_contents;
        for(;;) {
            qa_collision_terminal at;
            if(!hull_contents(hull,frame->mid,hull->root,&at,error)) return false;
            if(!blocks(at.bits,policy)) break;
            frame->fraction-=0.1f;
            if(frame->fraction<0) { trace.fraction=frame->midf; trace.end=frame->mid; *out=trace; return true; }
            frame->midf=frame->p1f+(frame->p2f-frame->p1f)*frame->fraction;
            frame->mid=qa_vec_lerp(frame->p1,frame->p2,frame->fraction);
        }
        trace.fraction=frame->midf; trace.end=frame->mid; *out=trace; return true;
    }
    *out=trace; return true;
}
static q1hull model_hull(const q1state *state,size_t model,unsigned index) {
    return (q1hull){index==0?state->drawing:state->clip,index==0?state->drawing_count:state->clip_count,
        state->planes,state->plane_count,state->terminals,index==0?state->models[model].drawing_root:state->models[model].clip_roots[index-1]};
}
static bool same_vec(qa_vec3 a,qa_vec3 b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static void native_result(const qa_trace_query *query,const native_trace *trace,qa_vec3 origin,const qa_vec3 basis[3],qa_trace_result *out) {
    qa_trace_result result=qa_collision_empty_trace(query,QA_GAME_Q1);
    result.fraction=trace->fraction;
    result.end=trace->fraction==1?query->end:qa_vec_add(qa_collision_from_local(trace->end,basis),origin);
    result.start_solid=trace->start_solid; result.all_solid=trace->all_solid;
    result.in_open=trace->in_open; result.in_water=trace->in_water;
    result.plane=qa_collision_make_plane(qa_collision_pose_normal(trace->plane.normal,&query->target,query->target.inline_model,basis),trace->plane.distance,trace->plane.type);
    result.contact_plane=result.plane;
    result.contact_plane.distance+=qa_vec_dot(result.contact_plane.normal,origin);
    result.contents=trace->contents.bits; result.q1_opaque_token=trace->contents.opaque_token; result.contact=trace->fraction<1;
    result.hit=trace->fraction<1||trace->start_solid?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE;
    const qa_collision_brush_rules rules=qa_collision_rules(&query->policy).brush;
    if(trace->all_solid && (rules.zero_all_solid || (rules.zero_stationary && same_vec(query->start,query->end)))) {
        result.fraction=0; result.end=query->start; result.contact=false;
    }
    *out=result;
}
static bool select_model(const q1state *state,const qa_collision_target *target,size_t *model,qa_vec3 *origin,qa_vec3 basis[3],qa_error *error) {
    *model=target->inline_model?target->model:0;
    if(*model>=state->model_count) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unknown Quake collision model"); return false; }
    *origin=target->inline_model?target->origin:qa_v3(0,0,0);
    qa_collision_pose_basis(target,target->inline_model,basis); return true;
}
static q1p derived_plane(qa_collision_plane p) { return (q1p){qfrom(p.normal),p.distance}; }

/* These stacks retain one entry per BSP depth. Repeated solid leaf references
 * are distinct cells, because their enclosing plane paths differ. */
typedef struct cell_frame { int32_t node; q1cell *cell; size_t depth; } cell_frame;
typedef bool (*cell_consumer)(q1work *,q1cell *,qa_collision_terminal,void *);
static bool hull_cells(q1work *w,const q1hull *hull,q1bounds envelope,bool free_only,const qa_trace_policy *policy,cell_consumer consume,void *context) {
    if(hull->count==SIZE_MAX) { qa_error_set(w->error,QA_ERROR_MEMORY,0,"Quake cell depth overflow"); return false; }
    cell_frame *stack=q1_alloc(w,hull->count+1,sizeof(*stack),alignof(cell_frame));
    q1cell *box=q1_box(w,envelope);
    if(w->failed) return false;
    size_t count=1; stack[0]=(cell_frame){hull->root,box,0};
    while(count>0) {
        cell_frame frame=stack[--count];
        if(frame.node<0) {
            qa_collision_terminal terminal=hull_terminal(hull,frame.node);
            bool include=free_only?!qa_collision_bits_overlap(terminal.bits,qa_collision_bit(QA_CONTENT_SOLID)):blocks(terminal.bits,policy);
            /* A consumer can finish early without an error, for example once
             * subtraction has proved that no clip-only solid remains. */
            if(include && !consume(w,frame.cell,terminal,context)) return !w->failed;
            continue;
        }
        if(frame.depth>hull->count) { qa_error_set(w->error,QA_ERROR_FORMAT,0,"Cycle in Quake solid-space BSP"); return false; }
        const q1node *node;
        /* Cell derivation uses indexed clipnodes; source hull range checks are
         * specific to native RecursiveHullCheck and are kept there. */
        if((size_t)frame.node>=hull->count) { qa_error_set(w->error,QA_ERROR_FORMAT,0,"Invalid Quake solid-space node"); return false; }
        node=&hull->nodes[frame.node];
        if(node->plane>=hull->plane_count) { qa_error_set(w->error,QA_ERROR_FORMAT,0,"Invalid Quake solid-space plane"); return false; }
        q1cell *front,*back;
        q1_split(w,frame.cell,derived_plane(hull->planes[node->plane]),&front,&back);
        if(w->failed) return false;
        size_t added=(front!=NULL?1u:0u)+(back!=NULL?1u:0u);
        if(added>hull->count+1-count) { qa_error_set(w->error,QA_ERROR_FORMAT,0,"Cycle in Quake solid-space BSP"); return false; }
        if(back!=NULL) stack[count++]=(cell_frame){node->children[1],back,frame.depth+1};
        if(front!=NULL) stack[count++]=(cell_frame){node->children[0],front,frame.depth+1};
    }
    return true;
}
typedef struct clip_context { q1cells solids; qa_bounds hull_bounds; } clip_context;
static bool subtract_free(q1work *w,q1cell *free_cell,qa_collision_terminal contents,void *context) {
    (void)contents;
    clip_context *clip=context; q1planes planes={0};
    if(!q1_separating_planes(w,free_cell,cell_axes,&planes)) return false;
    q1v center=qscale(qadd(qfrom(clip->hull_bounds.mins),qfrom(clip->hull_bounds.maxs)),0.5);
    q1v extents=qscale(qsub(qfrom(clip->hull_bounds.maxs),qfrom(clip->hull_bounds.mins)),0.5);
    for(size_t i=0;i<planes.count;i++) {
        q1v n=planes.items[i].normal;
        planes.items[i].distance+=qdot(n,center)+fabs(n.x)*extents.x+fabs(n.y)*extents.y+fabs(n.z)*extents.z;
    }
    q1cells remainder={0};
    for(size_t i=0;i<clip->solids.count;i++) {
        q1cell *inside=clip->solids.items[i];
        for(size_t p=0;p<planes.count && inside!=NULL;p++) {
            q1cell *outside,*back;
            q1_split(w,inside,planes.items[p],&outside,&back);
            if(w->failed) return false;
            if(outside!=NULL && !q1_cells_push(w,&remainder,outside)) return false;
            inside=back;
        }
    }
    clip->solids=remainder; return remainder.count!=0;
}
static bool derive_clip(q1work *w,const q1state *state,size_t model,q1bounds envelope,q1cells *out) {
    clip_context context={0};
    q1cell *initial=q1_box(w,envelope);
    if(initial==NULL || !q1_cells_push(w,&context.solids,initial)) return false;
    bool used=false;
    for(unsigned i=1;i<3;i++) {
        q1hull hull=model_hull(state,model,i);
        if(hull.root<0 && !qa_collision_bits_overlap(hull_terminal(&hull,hull.root).bits,qa_collision_bit(QA_CONTENT_SOLID))) continue;
        used=true; context.hull_bounds=hull_bounds[i];
        q1v one=qv(1,1,1);
        q1bounds free_envelope={qsub(envelope.min,qadd(qfrom(hull_bounds[i].maxs),one)),
                              qsub(envelope.max,qsub(qfrom(hull_bounds[i].mins),one))};
        if(!hull_cells(w,&hull,free_envelope,true,NULL,subtract_free,&context)) return false;
        if(context.solids.count==0) break;
    }
    *out=used?context.solids:(q1cells){0}; return true;
}
typedef struct sweep_context {
    q1v start,end; q1shape shape;
    double epsilon;
    q1interval *intervals; size_t count,capacity;
} sweep_context;
static bool sweep_cell(q1work *w,q1cell *cell,qa_collision_terminal contents,void *context) {
    sweep_context *sweep=context; q1interval interval;
    bool hit=q1_sweep_cell(w,cell,sweep->start,sweep->end,&sweep->shape,sweep->epsilon,&interval);
    if(w->failed) return false;
    if(!hit) return true;
    void *data=sweep->intervals;
    if(!q1_grow(w,&data,&sweep->capacity,sweep->count+1,sizeof(*sweep->intervals),alignof(q1interval))) return false;
    sweep->intervals=data;
    interval.contents=contents; interval.order=sweep->count;
    sweep->intervals[sweep->count++]=interval; return true;
}
static q1bounds sweep_envelope(const sweep_context *sweep,q1v end) {
    q1v pad=qv(q1_shape_support(&sweep->shape,cell_axes[0])+1,q1_shape_support(&sweep->shape,cell_axes[1])+1,q1_shape_support(&sweep->shape,cell_axes[2])+1);
    return (q1bounds){qv(fmin(sweep->start.x,end.x)-pad.x,fmin(sweep->start.y,end.y)-pad.y,fmin(sweep->start.z,end.z)-pad.z),
                     qv(fmax(sweep->start.x,end.x)+pad.x,fmax(sweep->start.y,end.y)+pad.y,fmax(sweep->start.z,end.z)+pad.z)};
}
static int interval_compare(const void *a,const void *b) {
    const q1interval *first=a,*second=b;
    if(first->enter<second->enter) return -1;
    if(first->enter>second->enter) return 1;
    return first->order<second->order?-1:first->order>second->order?1:0;
}
static q1v local_derived(q1v v,const qa_vec3 basis[3]) {
    return qv(qdot(v,qfrom(basis[0])),qdot(v,qfrom(basis[1])),qdot(v,qfrom(basis[2])));
}
static bool same_coordinate(double a,double b) { return memcmp(&a,&b,sizeof(a))==0; }
static bool same_envelope(q1bounds a,q1bounds b) {
    return same_coordinate(a.min.x,b.min.x)&&same_coordinate(a.min.y,b.min.y)&&same_coordinate(a.min.z,b.min.z)
        &&same_coordinate(a.max.x,b.max.x)&&same_coordinate(a.max.y,b.max.y)&&same_coordinate(a.max.z,b.max.z);
}
static void cache_unlink(q1scratch *scratch,q1clip_cache *cache) {
    if(cache->previous!=NULL) cache->previous->next=cache->next; else scratch->cache_first=cache->next;
    if(cache->next!=NULL) cache->next->previous=cache->previous; else scratch->cache_last=cache->previous;
    cache->previous=NULL; cache->next=NULL;
}
static void cache_append(q1scratch *scratch,q1clip_cache *cache) {
    cache->previous=scratch->cache_last;
    if(scratch->cache_last!=NULL) scratch->cache_last->next=cache; else scratch->cache_first=cache;
    scratch->cache_last=cache;
}
static void cache_remove(q1scratch *scratch,q1clip_cache *cache) {
    cache_unlink(scratch,cache);
    scratch->cache_count--; scratch->cache_cells-=cache->cells.count;
    scratch->cache_faces-=cache->face_count; scratch->cache_vertices-=cache->vertex_count;
    *cache=(q1clip_cache){.model=SIZE_MAX};
}
static void cache_clear(q1scratch *scratch) {
    while(scratch->cache_first!=NULL) cache_remove(scratch,scratch->cache_first);
    qa_arena_reset(&scratch->cache_storage[0]);
    qa_arena_reset(&scratch->cache_storage[1]);
}
static bool cache_copy(q1work *work,const q1cells *source,q1cells *out) {
    q1cells copy={.capacity=source->count};
    copy.items=q1_alloc(work,source->count,sizeof(*copy.items),alignof(q1cell *));
    for(size_t i=0;i<source->count && !work->failed;i++) {
        q1cell *cell=q1_alloc(work,1,sizeof(*cell),alignof(q1cell));
        if(cell==NULL) break;
        *cell=*source->items[i];
        cell->faces=q1_alloc(work,cell->count,sizeof(*cell->faces),alignof(q1face));
        if(work->failed) break;
        for(size_t f=0;f<cell->count;f++) {
            cell->faces[f]=source->items[i]->faces[f];
            cell->faces[f].vertices=q1_alloc(work,cell->faces[f].count,sizeof(q1v),alignof(q1v));
            if(work->failed) break;
            memcpy(cell->faces[f].vertices,source->items[i]->faces[f].vertices,cell->faces[f].count*sizeof(q1v));
        }
        copy.items[copy.count++]=cell;
    }
    if(work->failed) return false;
    *out=copy; return true;
}
/* Reclaim evicted payloads in the other reserved arena. Entry addresses and
 * LRU order stay fixed; only completed cell storage moves. */
static bool cache_compact(q1scratch *scratch) {
    unsigned next=scratch->active_cache^1u;
    qa_arena_reset(&scratch->cache_storage[next]);
    q1work work={&scratch->cache_storage[next],NULL,false};
    for(q1clip_cache *cache=scratch->cache_first;cache!=NULL;cache=cache->next) {
        q1cells copy;
        if(!cache_copy(&work,&cache->cells,&copy)) { cache_clear(scratch); return false; }
        cache->cells=copy;
    }
    qa_arena_reset(&scratch->cache_storage[scratch->active_cache]);
    scratch->active_cache=next; return true;
}
/* Cache capacity includes every payload and alignment gap. Intermediate
 * clipping remains in query scratch; a full cache never changes the trace. */
static void cache_store(q1scratch *scratch,size_t model,q1bounds envelope,const q1cells *cells) {
    if(cells->count>Q1_CACHE_CELLS) return;
    size_t faces=0,vertices=0;
    for(size_t i=0;i<cells->count;i++) {
        if(cells->items[i]->count>Q1_CACHE_FACES-faces) return;
        faces+=cells->items[i]->count;
        for(size_t f=0;f<cells->items[i]->count;f++) {
            if(cells->items[i]->faces[f].count>Q1_CACHE_VERTICES-vertices) return;
            vertices+=cells->items[i]->faces[f].count;
        }
    }
    bool evicted=false;
    while(scratch->cache_first!=NULL && (scratch->cache_count>=Q1_CACHE_ENTRIES || scratch->cache_cells+cells->count>Q1_CACHE_CELLS
        ||scratch->cache_faces+faces>Q1_CACHE_FACES || scratch->cache_vertices+vertices>Q1_CACHE_VERTICES)) {
        cache_remove(scratch,scratch->cache_first); evicted=true;
    }
    if(evicted && !cache_compact(scratch)) return;
    q1clip_cache *cache=NULL;
    for(size_t i=0;i<Q1_CACHE_ENTRIES;i++) if(scratch->entries[i].model==SIZE_MAX) { cache=&scratch->entries[i]; break; }
    if(cache==NULL) return;
    q1work work={&scratch->cache_storage[scratch->active_cache],NULL,false};
    q1cells copy;
    if(!cache_copy(&work,cells,&copy)) { cache_clear(scratch); return; }
    *cache=(q1clip_cache){.model=model,.envelope=envelope,.face_count=faces,.vertex_count=vertices,.cells=copy};
    cache_append(scratch,cache);
    scratch->cache_count++; scratch->cache_cells+=cells->count; scratch->cache_faces+=faces; scratch->cache_vertices+=vertices;
}
static bool cached_clip(q1work *w,const q1state *state,q1scratch *scratch,size_t model,q1bounds envelope,q1cells *out) {
    for(q1clip_cache *cache=scratch->cache_first;cache!=NULL;cache=cache->next) {
        if(cache->model!=model || !same_envelope(cache->envelope,envelope)) continue;
        cache_unlink(scratch,cache); cache_append(scratch,cache); *out=cache->cells; return true;
    }
    if(!derive_clip(w,state,model,envelope,out)) return false;
    cache_store(scratch,model,envelope,out); return true;
}
static bool collect_clip(q1work *w,const q1state *state,q1scratch *scratch,size_t model,q1bounds envelope,sweep_context *sweep,bool *starts_solid) {
    q1cells clip={0};
    if(!cached_clip(w,state,scratch,model,envelope,&clip)) return false;
    *starts_solid=false;
    for(size_t i=0;i<clip.count;i++) {
        size_t before=sweep->count;
        if(!sweep_cell(w,clip.items[i],(qa_collision_terminal){qa_collision_bit(QA_CONTENT_SOLID),0},sweep)) return false;
        if(sweep->count>before && sweep->intervals[before].enter<=0) *starts_solid=true;
    }
    return true;
}
static bool arbitrary_trace(const q1state *state,q1scratch *scratch,q1work *w,const qa_trace_query *query,size_t model,qa_vec3 origin,const qa_vec3 basis[3],qa_trace_result *out) {
    qa_bounds bounds=query->shape.bounds;
    q1v center=qscale(qadd(qfrom(bounds.mins),qfrom(bounds.maxs)),0.5);
    q1v extents=qscale(qsub(qfrom(bounds.maxs),qfrom(bounds.mins)),0.5);
    sweep_context sweep={0};
    const qa_collision_brush_rules rules=qa_collision_rules(&query->policy).brush;
    sweep.epsilon=rules.epsilon;
    sweep.start=local_derived(qsub(qadd(qfrom(query->start),center),qfrom(origin)),basis);
    sweep.end=local_derived(qsub(qadd(qfrom(query->end),center),qfrom(origin)),basis);
    sweep.shape.capsule=query->shape.kind==QA_SHAPE_CAPSULE; sweep.shape.extents=extents;
    for(unsigned i=0;i<3;i++) sweep.shape.axes[i]=local_derived(cell_axes[i],basis);
    sweep.shape.radius=fmin(extents.x,fmin(extents.y,extents.z));
    sweep.shape.half_segment=fmax(0,extents.z-sweep.shape.radius);
    q1bounds envelope=sweep_envelope(&sweep,sweep.end);
    if(state->models[model].authored_brushes) {
        qa_bsp_range range=state->models[model].brushes;
        for(size_t i=range.first;i<(size_t)range.first+range.count;i++) {
            if(!blocks(state->brush_contents[i].bits,&query->policy)||state->brush_cells[i]==NULL) continue;
            q1bounds b=state->brush_cells[i]->bounds;
            if(b.min.x>envelope.max.x||b.max.x<envelope.min.x||b.min.y>envelope.max.y||b.max.y<envelope.min.y||b.min.z>envelope.max.z||b.max.z<envelope.min.z) continue;
            if(!sweep_cell(w,state->brush_cells[i],state->brush_contents[i],&sweep)) return false;
        }
    } else {
        q1hull drawing=model_hull(state,model,0);
        if(!hull_cells(w,&drawing,envelope,false,&query->policy,sweep_cell,&sweep)) return false;
        if(blocks(qa_collision_bit(QA_CONTENT_SOLID),&query->policy)) {
            size_t drawing_count=sweep.count; double first=1;
            for(size_t i=0;i<sweep.count;i++) first=fmin(first,sweep.intervals[i].enter);
            bool prefix=first>0 && first<1,starts_solid;
            q1bounds clipped=prefix?sweep_envelope(&sweep,qlerp(sweep.start,sweep.end,first)):envelope;
            if(!collect_clip(w,state,scratch,model,clipped,&sweep,&starts_solid)) return false;
            if(starts_solid && prefix) { sweep.count=drawing_count; if(!collect_clip(w,state,scratch,model,envelope,&sweep,&starts_solid)) return false; }
        }
    }
    if(sweep.count>1) qsort(sweep.intervals,sweep.count,sizeof(*sweep.intervals),interval_compare);
    bool start_solid=false; double covered=-INFINITY,fraction=1; q1p plane={{0,0,0},0}; qa_collision_terminal contents={0};
    for(size_t i=0;i<sweep.count;i++) {
        q1interval interval=sweep.intervals[i];
        if(interval.enter<=0) { start_solid=true; contents=interval.contents; covered=fmax(covered,interval.exit); continue; }
        if(start_solid && interval.enter<=covered+1e-8) { covered=fmax(covered,interval.exit); continue; }
        fraction=fmax(0,interval.contact); plane=interval.plane; contents=interval.contents; break;
    }
    q1hull point_hull=model_hull(state,model,0); native_trace environment;
    qa_vec3 reached=qa_vec_lerp(query->start,query->end,(float)fraction);
    if(!trace_hull(scratch->frames,&point_hull,qa_collision_to_local(qa_vec_sub(query->start,origin),basis),
        qa_collision_to_local(qa_vec_sub(reached,origin),basis),NULL,&environment,w->error)) return false;
    bool all_solid=start_solid && covered>=1;
    if(all_solid && (rules.zero_all_solid || (rules.zero_stationary && same_vec(query->start,query->end)))) fraction=0;
    reached=qa_vec_lerp(query->start,query->end,(float)fraction);
    qa_trace_result result=qa_collision_empty_trace(query,QA_GAME_Q1);
    result.fraction=(float)fraction; result.end=fraction==1?query->end:reached;
    result.start_solid=start_solid; result.all_solid=all_solid;
    result.in_open=environment.in_open; result.in_water=environment.in_water;
    result.plane=qa_collision_make_plane(qa_collision_pose_normal(qto(plane.normal),&query->target,query->target.inline_model,basis),(float)plane.distance,3);
    result.contact_plane=result.plane; result.contact_plane.distance+=qa_vec_dot(result.plane.normal,origin);
    result.contact=fraction<1 && (query->policy.family==QA_GAME_Q1 || !all_solid);
    result.hit=fraction<1||start_solid?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE;
    result.contents=contents.bits; result.q1_opaque_token=contents.opaque_token; *out=result; return true;
}

static uint32_t plane_hash(qa_collision_plane plane) {
    float values[4]={plane.normal.x,plane.normal.y,plane.normal.z,plane.distance};
    uint32_t hash=UINT32_C(2166136261);
    for(unsigned i=0;i<4;i++) {
        uint32_t word=0;
        if(values[i]!=0) memcpy(&word,&values[i],sizeof(word));
        hash=(hash^word)*UINT32_C(16777619);
    }
    return hash;
}
static bool face_vertex(const q1state *state,const qa_bsp_face *face,size_t corner,qa_vec3 *out,qa_error *error) {
    int64_t signed_edge;
    if(!qa_bsp_read_index(&state->bsp,QA_BSP_SURFEDGES,(size_t)face->edges.first+corner,&signed_edge,error)) return false;
    size_t edge_index=(size_t)(signed_edge<0?-signed_edge:signed_edge);
    qa_bsp_edge edge; qa_bsp_vertex vertex;
    if(!qa_bsp_read_edge(&state->bsp,edge_index,&edge,error)
        || !qa_bsp_read_vertex(&state->bsp,edge.vertices[signed_edge<0?1:0],&vertex,error)) return false;
    *out=qa_bsp_to_vec(vertex.position); return true;
}
static bool surface_at_contact(const q1state *state,size_t model,qa_vec3 point,qa_collision_plane plane,qa_trace_result *result,qa_error *error) {
    const q1model *m=&state->models[model];
    if(m->bucket_count==0) return true;
    q1v normal=qfrom(plane.normal); double magnitude=qdot(normal,normal);
    if(magnitude==0) return true;
    q1v projected=qsub(qfrom(point),qscale(normal,(qdot(qfrom(point),normal)-plane.distance)/magnitude));
    size_t index=m->face_buckets[plane_hash(plane)%m->bucket_count];
    while(index!=SIZE_MAX) {
        const q1contactface *candidate=&m->contact_faces[index]; index=candidate->next;
        if(!same_vec(candidate->plane.normal,plane.normal)||candidate->plane.distance!=plane.distance) continue;
        const qa_bsp_face *face=&candidate->face;
        bool positive=false,negative=false;
        for(size_t i=0;i<face->edges.count;i++) {
            qa_vec3 a,b;
            if(!face_vertex(state,face,i,&a,error)||!face_vertex(state,face,(i+1)%face->edges.count,&b,error)) return false;
            double side=qdot(qcross(qsub(qfrom(b),qfrom(a)),qsub(projected,qfrom(a))),normal);
            positive|=side>0; negative|=side<0;
            if(positive && negative) break;
        }
        if(face->edges.count<3 || (positive && negative)) continue;
        if(!candidate->has_surface) return true;
        result->surface_flags=candidate->surface.flags;
        result->has_surface=true; result->surface=candidate->surface;
        return true;
    }
    return true;
}
static bool q1_trace(const void *opaque,void *workspace,const qa_trace_query *query,qa_trace_result *out,qa_error *error) {
    const q1state *state=opaque; q1scratch *scratch=workspace;
    qa_arena_reset(&scratch->scratch); q1work work={&scratch->scratch,error,false};
    size_t model; qa_vec3 origin,basis[3];
    if(!select_model(state,&query->target,&model,&origin,basis,error)) return false;
    qa_bounds bounds=query->shape.kind==QA_SHAPE_POINT?hull_bounds[0]:query->shape.bounds;
    int32_t hull_index=query->shape.kind==QA_SHAPE_POINT?0:-1;
    if(query->policy.family==QA_GAME_Q1) {
        if(query->policy.q1_hull>=0) hull_index=query->policy.q1_hull;
        else if(query->shape.kind==QA_SHAPE_BOX) {
            float width=bounds.maxs.x-bounds.mins.x;
            hull_index=width<3?0:width<=32?1:2;
        }
    }
    if(hull_index>=0) {
        if(hull_index>=3) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unsupported Quake native hull %d",hull_index); return false; }
        q1hull hull=model_hull(state,model,(unsigned)hull_index);
        qa_vec3 offset=qa_vec_add(origin,qa_vec_sub(hull_bounds[hull_index].mins,bounds.mins));
        native_trace trace;
        if(!trace_hull(scratch->frames,&hull,qa_collision_to_local(qa_vec_sub(query->start,offset),basis),
            qa_collision_to_local(qa_vec_sub(query->end,offset),basis),&query->policy,&trace,error)) return false;
        qa_trace_result result;
        native_result(query,&trace,offset,basis,&result);
        if(hull_index==0 && trace.fraction<1 && !trace.all_solid
            && !surface_at_contact(state,model,trace.end,trace.plane,&result,error)) return false;
        *out=result; return true;
    }
    return arbitrary_trace(state,scratch,&work,query,model,origin,basis,out);
}
static bool q1_point_contents(const void *opaque,void *workspace,const qa_point_query *query,qa_point_contents *out,qa_error *error) {
    const q1state *state=opaque; (void)workspace;
    size_t model; qa_vec3 origin,basis[3];
    if(!select_model(state,&query->target,&model,&origin,basis,error)) return false;
    q1hull hull=model_hull(state,model,0); qa_collision_terminal contents;
    if(!hull_contents(&hull,qa_collision_to_local(qa_vec_sub(query->point,origin),basis),hull.root,&contents,error)) return false;
    *out=(qa_point_contents){.family=QA_GAME_Q1,.contents=contents.bits,.stored=contents.bits,.merged=contents.bits,
        .q1_opaque_token=contents.opaque_token}; return true;
}
static void q1_destroy(void *opaque) {
    q1state *state=opaque;
    if(state==NULL) return;
    qa_arena_destroy(&state->retained); free(state);
}
static void q1_destroy_scratch(void *opaque) {
    q1scratch *scratch=opaque;
    if(scratch==NULL) return;
    qa_arena_destroy(&scratch->scratch);
    qa_arena_destroy(&scratch->cache_storage[0]);
    qa_arena_destroy(&scratch->cache_storage[1]);
    free(scratch->frames); free(scratch);
}
static void *q1_create_scratch(const void *opaque,qa_error *error) {
    const q1state *state=opaque;
    size_t topology=state->drawing_count;
    size_t additions[]={state->clip_count,state->brush_count,state->brush_plane_count,1};
    for(size_t i=0;i<sizeof(additions)/sizeof(*additions);i++) {
        if(additions[i]>SIZE_MAX-topology) { qa_error_set(error,QA_ERROR_MEMORY,0,"Quake collision scratch topology overflow"); return NULL; }
        topology+=additions[i];
    }
    /* Retail 1024-unit cross-policy sweeps peak below 258 bytes per loaded
     * node. Reserve about four times that measured amount, scaling with map data. */
    /* Cell, separating-plane and sweep arrays also have fixed bootstrap
     * storage: the one-node gameplay floor uses 3291 bytes per query. */
    const size_t bootstrap_bytes=4096;
    if(topology>(SIZE_MAX-bootstrap_bytes)/1024) { qa_error_set(error,QA_ERROR_MEMORY,0,"Quake collision scratch size overflow"); return NULL; }
    size_t query_bytes=bootstrap_bytes+topology*1024;
    size_t frame_count=(state->drawing_count>state->clip_count?state->drawing_count:state->clip_count)+1;
    if(frame_count==0 || frame_count>SIZE_MAX/sizeof(hull_frame)) { qa_error_set(error,QA_ERROR_MEMORY,0,"Quake collision continuation size overflow"); return NULL; }
    size_t cache_bytes=Q1_CACHE_CELLS*(sizeof(q1cell *)+alignof(q1cell *)-1+sizeof(q1cell)+alignof(q1cell)-1+alignof(q1face)-1)
        +Q1_CACHE_FACES*(sizeof(q1face)+alignof(q1v)-1)+Q1_CACHE_VERTICES*sizeof(q1v);
    q1scratch *scratch=calloc(1,sizeof(*scratch));
    if(scratch==NULL) { qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Quake collision scratch"); return NULL; }
    scratch->frames=malloc(frame_count*sizeof(*scratch->frames));
    scratch->frame_capacity=frame_count;
    if(scratch->frames==NULL) { qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Quake hull continuations"); goto fail; }
    for(size_t i=0;i<Q1_CACHE_ENTRIES;i++) scratch->entries[i].model=SIZE_MAX;
    if(!qa_arena_reserve(&scratch->scratch,query_bytes,error)
        || !qa_arena_reserve(&scratch->cache_storage[0],cache_bytes,error)
        || !qa_arena_reserve(&scratch->cache_storage[1],cache_bytes,error)) goto fail;
    qa_arena_seal(&scratch->scratch);
    qa_arena_seal(&scratch->cache_storage[0]);
    qa_arena_seal(&scratch->cache_storage[1]);
    return scratch;
fail:
    q1_destroy_scratch(scratch); return NULL;
}
static const qa_collision_ops q1_ops={.destroy=q1_destroy,.trace=q1_trace,.point_contents=q1_point_contents,
    .create_scratch=q1_create_scratch,.destroy_scratch=q1_destroy_scratch};

static bool build_contact_faces(q1work *work,q1state *state,q1model *model) {
    size_t count=model->source.faces.count;
    if(count==0) return true;
    size_t texture_count;
    if(!qa_bsp_texture_count(&state->bsp,&texture_count,work->error)) return false;
    model->contact_faces=q1_alloc(work,count,sizeof(*model->contact_faces),alignof(q1contactface));
    model->face_buckets=q1_alloc(work,count,sizeof(*model->face_buckets),alignof(size_t));
    if(work->failed) return false;
    model->bucket_count=count;
    for(size_t i=0;i<count;i++) model->face_buckets[i]=SIZE_MAX;
    /* Reverse insertion keeps the donor's first authored face tie order. */
    for(size_t i=count;i>0;i--) {
        size_t index=i-1; qa_bsp_face face;
        if(!qa_bsp_read_face(&state->bsp,(size_t)model->source.faces.first+index,&face,work->error)) return false;
        if(face.plane>=state->plane_count) { qa_error_set(work->error,QA_ERROR_FORMAT,0,"Invalid Quake contact face plane"); return false; }
        qa_collision_plane plane=state->planes[face.plane];
        if(face.draw_flags!=0) { plane.normal=qa_vec_scale(plane.normal,-1); plane.distance=-plane.distance; }
        size_t bucket=plane_hash(plane)%count;
        q1contactface contact={.face=face,.plane=plane,.next=model->face_buckets[bucket]};
        qa_bsp_texinfo info;
        if(!qa_bsp_read_texinfo(&state->bsp,face.texinfo,&info,work->error)) return false;
        if(info.texture>=0 && (size_t)info.texture<texture_count) {
            qa_bsp_texture texture;
            if(!qa_bsp_read_texture(&state->bsp,(size_t)info.texture,&texture,work->error)) return false;
            if(texture.storage!=QA_BSP_TEXTURE_MISSING) {
                contact.has_surface=true;
                contact.surface.flags=texture.name.size>=3 && memcmp(texture.name.data,"sky",3)==0
                    ?sky_surface_flags:(qa_collision_bits){0};
                size_t length=texture.name.size<sizeof(contact.surface.name)-1
                    ?texture.name.size:sizeof(contact.surface.name)-1;
                if(length>0) memcpy(contact.surface.name,texture.name.data,length);
            }
        }
        model->contact_faces[index]=contact;
        model->face_buckets[bucket]=index;
    }
    return true;
}
static bool load_brushes(q1work *work,q1state *state) {
    qa_bsp_extension extension;
    if(!qa_bsp_find_extension(&state->bsp,"BRUSHLIST",&extension,NULL)) return true;
    qa_error local={0}; qa_bsp_brush_list brushes={0};
    if(!qa_bsp_read_brush_list(&state->bsp,&brushes,&local)) {
        if(local.code==QA_ERROR_UNSUPPORTED) return true;
        if(work->error!=NULL) *work->error=local;
        return false;
    }
    bool ok=false;
    state->brush_count=brushes.brush_count; state->brush_plane_count=brushes.plane_count;
    state->brush_cells=q1_alloc(work,brushes.brush_count,sizeof(*state->brush_cells),alignof(q1cell *));
    state->brush_contents=q1_alloc(work,brushes.brush_count,sizeof(*state->brush_contents),alignof(qa_collision_terminal));
    if(work->failed) goto done;
    for(size_t i=0;i<brushes.model_count;i++) {
        qa_bsp_brush_model model=brushes.models[i];
        if(model.model>=state->model_count) { qa_error_set(work->error,QA_ERROR_FORMAT,0,"Invalid Quake BSPX brush model"); goto done; }
        if(!state->models[model.model].authored_brushes) {
            state->models[model.model].authored_brushes=true;
            state->models[model.model].brushes=model.brushes;
        }
    }
    for(size_t i=0;i<brushes.brush_count;i++) {
        qa_bsp_extra_brush brush=brushes.brushes[i];
        state->brush_contents[i]=qa_collision_q1_terminal(brush.contents);
        q1cell *cell=q1_box(work,qbounds(qa_bsp_to_bounds(brush.bounds)));
        for(size_t j=0;j<brush.planes.count && cell!=NULL;j++) {
            qa_bsp_plane p=brushes.planes[(size_t)brush.planes.first+j];
            cell=q1_clip(work,cell,derived_plane(qa_collision_bsp_plane(p)));
        }
        if(work->failed) goto done;
        state->brush_cells[i]=cell;
    }
    ok=true;
done:
    qa_bsp_brush_list_free(&brushes);
    return ok;
}
static int32_t terminal_reference(q1state *state,int32_t token) {
    if(token>=-19 && token<=-1) return token;
    size_t index=Q1_NAMED_TERMINALS;
    for(;index<state->terminal_count;index++) if(state->terminals[index].opaque_token==token) break;
    if(index==state->terminal_count) state->terminals[state->terminal_count++]=qa_collision_q1_terminal(token);
    return -(int32_t)(index+1);
}
bool qa_q1_collision_create(const qa_bsp_view *bsp,const qa_collision_topology *topology,qa_collision_kernel *out,qa_error *error) {
    if(bsp==NULL || out==NULL || bsp->family!=QA_BSP_Q1) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Quake collision requires a Q1 BSP"); return false; }
    q1state *state=calloc(1,sizeof(*state));
    if(state==NULL) { qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Quake collision geometry"); return false; }
    state->bsp=*bsp; qa_arena_init(&state->retained,65536);
    q1work work={&state->retained,error,false};
    state->planes=topology->planes; state->plane_count=topology->plane_count;
    state->drawing_count=qa_bsp_record_count(bsp,QA_BSP_NODES);
    state->clip_count=qa_bsp_record_count(bsp,QA_BSP_CLIPNODES);
    state->leaf_count=qa_bsp_record_count(bsp,QA_BSP_LEAVES);
    state->model_count=qa_bsp_record_count(bsp,QA_BSP_MODELS);
    if(state->model_count==0) { qa_error_set(error,QA_ERROR_FORMAT,0,"Quake BSP has no world model"); goto fail; }
    state->drawing=q1_alloc(&work,state->drawing_count,sizeof(*state->drawing),alignof(q1node));
    state->clip=q1_alloc(&work,state->clip_count,sizeof(*state->clip),alignof(q1node));
    state->leaf_references=q1_alloc(&work,state->leaf_count,sizeof(*state->leaf_references),alignof(int32_t));
    if(state->leaf_count>INT32_MAX-Q1_NAMED_TERMINALS || state->clip_count>(INT32_MAX-Q1_NAMED_TERMINALS-state->leaf_count)/2
        || state->model_count>(INT32_MAX-Q1_NAMED_TERMINALS-state->leaf_count-2*state->clip_count)/2) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Quake terminal table size overflow"); goto fail;
    }
    size_t terminal_capacity=Q1_NAMED_TERMINALS+state->leaf_count+2*state->clip_count+2*state->model_count;
    state->terminals=q1_alloc(&work,terminal_capacity,sizeof(*state->terminals),alignof(qa_collision_terminal));
    state->terminal_count=Q1_NAMED_TERMINALS;
    state->models=q1_alloc(&work,state->model_count,sizeof(*state->models),alignof(q1model));
    if(work.failed) goto fail;
    for(size_t i=0;i<Q1_NAMED_TERMINALS;i++) state->terminals[i]=qa_collision_q1_terminal(-(int32_t)(i+1));
    memset(state->models,0,state->model_count*sizeof(*state->models));
    for(size_t i=0;i<state->leaf_count;i++) {
        qa_bsp_leaf leaf;
        if(!qa_bsp_read_leaf(bsp,i,&leaf,error)) goto fail;
        if(leaf.contents>=0) { qa_error_set(error,QA_ERROR_FORMAT,0,"Quake leaf has nonnegative contents"); goto fail; }
        state->leaf_references[i]=terminal_reference(state,leaf.contents);
    }
    for(size_t i=0;i<state->drawing_count;i++) {
        qa_bsp_node node;
        if(!qa_bsp_read_node(bsp,i,&node,error)) goto fail;
        state->drawing[i]=(q1node){node.plane,{node.children[0],node.children[1]}};
        for(unsigned j=0;j<2;j++) if(node.children[j]<0) {
            size_t leaf=(size_t)(-1-(int64_t)node.children[j]);
            if(leaf>=state->leaf_count) { qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid Quake drawing hull leaf"); goto fail; }
            state->drawing[i].children[j]=state->leaf_references[leaf];
        }
    }
    for(size_t i=0;i<state->clip_count;i++) {
        qa_bsp_clipnode node;
        if(!qa_bsp_read_clipnode(bsp,i,&node,error)) goto fail;
        if(node.plane<0) { qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid Quake clipnode plane"); goto fail; }
        state->clip[i]=(q1node){(uint32_t)node.plane,{node.children[0],node.children[1]}};
        for(unsigned j=0;j<2;j++) if(node.children[j]<0) state->clip[i].children[j]=terminal_reference(state,node.children[j]);
    }
    for(size_t i=0;i<state->model_count;i++) {
        q1model *model=&state->models[i];
        *model=(q1model){0};
        if(!qa_bsp_read_model(bsp,i,&model->source,error)) goto fail;
        model->drawing_root=model->source.headnodes[0];
        if(model->drawing_root<0) {
            size_t leaf=(size_t)(-1-(int64_t)model->drawing_root);
            if(leaf>=state->leaf_count) { qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid Quake drawing hull root leaf"); goto fail; }
            model->drawing_root=state->leaf_references[leaf];
        }
        for(unsigned j=0;j<2;j++) model->clip_roots[j]=model->source.headnodes[j+1]<0
            ?terminal_reference(state,model->source.headnodes[j+1]):model->source.headnodes[j+1];
        if(!build_contact_faces(&work,state,model)) goto fail;
    }
    if(!load_brushes(&work,state)) goto fail;
    qa_arena_seal(&state->retained);
    *out=(qa_collision_kernel){state,&q1_ops}; return true;
fail:
    q1_destroy(state); return false;
}

bool qa_q1_trace_box(const qa_trace_query *query,qa_bounds target,qa_vec3 origin,qa_trace_result *out,qa_error *error) {
    if(query==NULL || out==NULL || query->shape.kind==QA_SHAPE_CAPSULE || !qa_bounds_valid(target)
        || !qa_vec_finite(origin) || !qa_vec_finite(query->start) || !qa_vec_finite(query->end)
        || (query->shape.kind!=QA_SHAPE_POINT && !qa_bounds_valid(query->shape.bounds))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Quake actor box trace"); return false;
    }
    qa_bounds moving=query->shape.kind==QA_SHAPE_POINT?hull_bounds[0]:query->shape.bounds;
    qa_bounds expanded={qa_vec_sub(target.mins,moving.maxs),qa_vec_sub(target.maxs,moving.mins)};
    if(!qa_bounds_valid(expanded)) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Quake actor trace bounds overflow"); return false; }
    qa_collision_plane planes[6]; q1node nodes[6];
    for(unsigned i=0;i<6;i++) {
        unsigned axis=i/2;
        qa_vec3 normal=qa_v3(0,0,0); qa_vec_set_component(&normal,axis,1);
        planes[i]=qa_collision_make_plane(normal,qa_vec_component(i%2==0?expanded.maxs:expanded.mins,axis),(int32_t)axis);
        int32_t next=i==5?Q1_SOLID_REFERENCE:(int32_t)i+1;
        nodes[i]=(q1node){i,{i%2==0?Q1_EMPTY_REFERENCE:next,i%2==0?next:Q1_EMPTY_REFERENCE}};
    }
    const qa_collision_terminal terminals[2]={{{0},0},{qa_collision_bit(QA_CONTENT_SOLID),0}};
    q1hull hull={nodes,6,planes,6,terminals,0}; hull_frame frames[7];
    native_trace trace;
    bool ok=trace_hull(frames,&hull,qa_vec_sub(query->start,origin),qa_vec_sub(query->end,origin),NULL,&trace,error);
    if(ok) {
        qa_vec3 basis[3]={qa_v3(1,0,0),qa_v3(0,1,0),qa_v3(0,0,1)};
        native_result(query,&trace,origin,basis,out);
        if(out->hit!=QA_TRACE_HIT_NONE) out->hit=QA_TRACE_HIT_ACTOR;
    }
    return ok;
}
