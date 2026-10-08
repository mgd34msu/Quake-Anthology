#include "internal.h"

static int32_t signed_bits(uint32_t bits) { int32_t value; memcpy(&value,&bits,sizeof(value)); return value; }

int32_t qa_collision_convert_contents(int32_t contents,qa_collision_family from,qa_collision_family to)
{
    if(from==to) return contents;
    if(from==QA_COLLISION_Q1) {
        switch(contents) {
            case -2: case -6: return 1;
            case -3: return 32;
            case -4: return 16;
            case -5: return 8;
            case -9: return 32|(to==QA_COLLISION_Q2?0x40000:0);
            case -10: return 32|(to==QA_COLLISION_Q2?0x80000:0);
            case -11: return 32|(to==QA_COLLISION_Q2?0x100000:0);
            case -12: return 32|(to==QA_COLLISION_Q2?0x200000:0);
            case -13: return 32|(to==QA_COLLISION_Q2?0x400000:0);
            case -14: return 32|(to==QA_COLLISION_Q2?0x800000:0);
            default: return 0;
        }
    }
    uint32_t bits=(uint32_t)contents;
    if(to==QA_COLLISION_Q1) {
        if((bits&(from==QA_COLLISION_Q2?UINT32_C(0xc6000003):UINT32_C(0x06000001)))!=0) return -2;
        if((bits&8u)!=0) return -5;
        if((bits&16u)!=0) return -4;
        if((bits&32u)!=0) return -3;
        return -1;
    }
    uint32_t result=bits&UINT32_C(0x0f038079);
    if(from==QA_COLLISION_Q2) {
        if((bits&2u)!=0) result|=1u;
        if((bits&UINT32_C(0xc0000000))!=0) result|=UINT32_C(0x02000000);
        if((bits&UINT32_C(0x10000000))!=0) result|=UINT32_C(0x20000000);
    } else {
        if((bits&UINT32_C(0x20000000))!=0) result|=UINT32_C(0x10000000);
        if((bits&UINT32_C(0x02000000))!=0) result|=UINT32_C(0x40000000);
    }
    return signed_bits(result);
}

int32_t qa_collision_convert_surface_flags(int32_t flags,qa_collision_family from,qa_collision_family to)
{
    if(from==to) return flags;
    if(from==QA_COLLISION_Q1 || to==QA_COLLISION_Q1) return 0;
    return (flags&0x86)|(from==QA_COLLISION_Q2 && to==QA_COLLISION_Q3 && (flags&4)!=0?16:0);
}

bool qa_collision_contents_block(int32_t contents,qa_collision_family family,const qa_trace_policy *policy)
{
    int32_t converted=qa_collision_convert_contents(contents,family,policy->family);
    return policy->family==QA_COLLISION_Q1?converted==-2:((uint32_t)converted&policy->contents_mask)!=0;
}

uint32_t qa_collision_geometry_mask(const qa_trace_policy *policy,qa_collision_family family)
{
    if(family==policy->family) return family==QA_COLLISION_Q1?0:policy->contents_mask;
    uint32_t mask=0;
    for(unsigned bit=0;bit<32;++bit) {
        uint32_t flag=UINT32_C(1)<<bit;
        if(qa_collision_contents_block(signed_bits(flag),family,policy)) mask|=flag;
    }
    return mask;
}

qa_trace_policy qa_collision_default_policy(qa_collision_family family)
{
    return (qa_trace_policy){.family=family,.contents_mask=UINT32_MAX,.q1_move=QA_Q1_MOVE_NORMAL,
        .q1_hull=-1,.q2_merged_contents=false,.curves=true,.player_curve_clip=true};
}

void qa_collision_adapt_trace(qa_trace_result *result,const qa_trace_policy *policy)
{
    qa_collision_family from=result->family,to=policy->family;
    if(from==to) return;
    int32_t native_contents=result->contents;
    bool sky=from==QA_COLLISION_Q1 && (native_contents==-6 || (result->surface_flags&4)!=0);
    int32_t surface_flags=result->surface_flags;
    if(from==QA_COLLISION_Q2) surface_flags=result->has_surface?result->surface.flags:0;
    if(from==QA_COLLISION_Q1) {
        qa_vec3 normal=result->contact?result->contact_plane.normal:result->plane.normal;
        int32_t type=normal.x==1.0f?0:normal.y==1.0f?1:normal.z==1.0f?2:3;
        result->plane=qa_collision_make_plane(normal,result->plane.distance,type);
    }
    result->family=to;
    result->contents=qa_collision_convert_contents(native_contents,from,to);
    if(to!=QA_COLLISION_Q2 || !policy->q2_merged_contents) {
        result->has_secondary=false; result->secondary_has_surface=false;
    }
    if(to==QA_COLLISION_Q1) {
        result->surface_flags=surface_flags;
        result->has_surface=false;
    } else if(to==QA_COLLISION_Q2) {
        memset(&result->surface,0,sizeof(result->surface));
        result->has_surface=from==QA_COLLISION_Q3 || sky || result->hit!=QA_TRACE_HIT_NONE;
        result->surface.flags=from==QA_COLLISION_Q3?qa_collision_convert_surface_flags(surface_flags,from,to):sky?4:0;
        if(sky) memcpy(result->surface.name,"sky",4);
        result->surface_flags=result->surface.flags;
        if(result->has_secondary) {
            result->secondary_has_surface=result->has_surface;
            result->secondary_surface=result->surface;
        }
    } else {
        result->has_surface=false;
        result->surface_flags=from==QA_COLLISION_Q2?qa_collision_convert_surface_flags(surface_flags,from,to):sky?20:0;
    }
}

void qa_collision_adapt_point(qa_point_contents *result,const qa_trace_policy *policy)
{
    if(result->family==policy->family) return;
    int32_t native=result->family==QA_COLLISION_Q2?result->merged:result->contents;
    int32_t contents=qa_collision_convert_contents(native,result->family,policy->family);
    *result=(qa_point_contents){policy->family,contents,contents,contents};
}

typedef struct q2_box_trace {
    qa_bounds expanded;
    qa_vec3 start,end;
} q2_box_trace;

static qa_collision_side_distances q2_box_distances(void *context,size_t index,bool need_last)
{
    const q2_box_trace *box=context;
    unsigned axis=(unsigned)(index/2);
    float sign=index%2==0?1.0f:-1.0f;
    float distance=qa_vec_component(index%2==0?box->expanded.maxs:box->expanded.mins,axis);
    return (qa_collision_side_distances){(qa_vec_component(box->start,axis)-distance)*sign,
        need_last?(qa_vec_component(box->end,axis)-distance)*sign:0};
}

static qa_collision_plane q2_box_plane(qa_bounds target,size_t index)
{
    unsigned axis=(unsigned)(index/2);
    float sign=index%2==0?1.0f:-1.0f;
    qa_vec3 normal={0}; qa_vec_set_component(&normal,axis,sign);
    return qa_collision_make_plane(normal,
        sign*qa_vec_component(index%2==0?target.maxs:target.mins,axis),
        (int32_t)(axis+(sign<0.0f?3u:0u)));
}

static bool trace_q2_box(const qa_trace_query *query,qa_bounds target,qa_vec3 origin,int32_t contents,qa_trace_result *out)
{
    qa_bounds moving=query->shape.kind==QA_SHAPE_POINT?(qa_bounds){0}:query->shape.bounds;
    q2_box_trace box={{qa_vec_sub(target.mins,moving.maxs),qa_vec_sub(target.maxs,moving.mins)},
        qa_vec_sub(query->start,origin),qa_vec_sub(query->end,origin)};
    qa_trace_result result=qa_collision_empty_trace(query,QA_COLLISION_Q2);
    bool stationary=query->start.x==query->end.x && query->start.y==query->end.y && query->start.z==query->end.z;
    const qa_collision_brush_rules rules=qa_collision_rules(&query->policy).brush;
    qa_collision_brush_contact contact;
    if(qa_collision_trace_brush(&box,q2_box_distances,0,6,stationary,&rules,contents,&result,&contact)) {
        result.plane=q2_box_plane(target,contact.side); result.contact_plane=result.plane;
        if(contact.secondary!=SIZE_MAX) {
            result.has_secondary=true; result.secondary_plane=q2_box_plane(target,contact.secondary);
        }
    }
    result.end=qa_vec_lerp(query->start,query->end,result.fraction);
    result.contact=result.fraction<1.0f&&!result.all_solid;
    result.hit=result.fraction<1.0f||result.start_solid?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE;
    *out=result; return true;
}

bool qa_collision_trace_body(const qa_trace_query *query,qa_collision_family actor_family,qa_shape_kind target_kind,qa_bounds target,qa_vec3 origin,int32_t contents,qa_trace_result *out,qa_error *error)
{
    if(query->policy.family==QA_COLLISION_Q1 && query->shape.kind!=QA_SHAPE_CAPSULE && target_kind!=QA_SHAPE_CAPSULE)
        return qa_q1_trace_box(query,target,origin,out,error);
    if(query->policy.family==QA_COLLISION_Q2 && query->shape.kind!=QA_SHAPE_CAPSULE && target_kind!=QA_SHAPE_CAPSULE)
        return trace_q2_box(query,target,origin,contents,out);
    bool native_q3=query->policy.family==QA_COLLISION_Q3 && actor_family==QA_COLLISION_Q3;
    qa_trace_query local=*query;
    local.target=(qa_collision_target){0};
    local.policy.contents_mask=native_q3?query->policy.contents_mask:
        (uint32_t)qa_collision_convert_contents(0x02000000,QA_COLLISION_Q3,query->policy.family);
    if(!qa_q3_trace_shape(&local,target_kind,target,origin,0x02000000,out,error)) return false;
    if(!native_q3) {
        bool occupied=out->fraction<1 || (query->policy.family==QA_COLLISION_Q1 && out->start_solid);
        out->contents=occupied?qa_collision_convert_contents(contents,query->policy.family,QA_COLLISION_Q3):0;
    }
    if(query->policy.family==QA_COLLISION_Q1) { out->in_open=!out->all_solid; out->in_water=(out->contents&56)!=0; }
    qa_collision_adapt_trace(out,&query->policy); return true;
}
