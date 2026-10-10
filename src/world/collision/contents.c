#include "internal.h"

int32_t qa_collision_tree_point(const qa_collision_plane *planes, const qa_collision_node *nodes,
    int32_t child, qa_vec3 point, bool axial_planes, bool front_on_plane)
{
    while (child >= 0) {
        const qa_collision_node *node = &nodes[(size_t)child];
        const qa_collision_plane *plane = &planes[node->plane];
        float distance = axial_planes ? qa_collision_plane_distance(point, plane)
            : qa_vec_dot(point, plane->normal) - plane->distance;
        child = node->children[front_on_plane ? (distance < 0 ? 1 : 0) : (distance > 0 ? 0 : 1)];
    }
    return child;
}

const qa_trace_behavior qa_trace_behaviors[QA_RULESET_Q3 + 1] = {
    [QA_RULESET_NETQUAKE] = {QA_GAME_Q1, {{.03125f, false, false, false, false}, {.03125f, 0, true}, false}, true, false, false, true, false},
    [QA_RULESET_QUAKEWORLD] = {QA_GAME_Q1, {{.03125f, false, false, false, false}, {.03125f, 0, true}, false}, true, false, false, true, false},
    [QA_RULESET_Q2_CLASSIC] = {QA_GAME_Q2, {{.03125f, false, false, false, true}, {.03125f, 0, true}, false}, false, true, false, true, false},
    [QA_RULESET_Q2_RERELEASE] = {QA_GAME_Q2, {{.03125f, true, true, true, true}, {.03125f, 0, true}, false}, false, true, false, true, true},
    [QA_RULESET_Q3] = {QA_GAME_Q3, {{.125f, true, false, true, true}, {.125f, 1, false}, true}, false, false, true, false, false}
};

const qa_collision_role_rules qa_collision_roles[QA_RULESET_Q3 + 1] = {
    [QA_RULESET_NETQUAKE] = {false, false, false},
    [QA_RULESET_QUAKEWORLD] = {false, false, false},
    [QA_RULESET_Q2_CLASSIC] = {true, true, false},
    [QA_RULESET_Q2_RERELEASE] = {true, true, false},
    [QA_RULESET_Q3] = {true, false, true}
};

qa_ruleset_id qa_collision_source_rules(qa_game_family family)
{
    static const qa_ruleset_id source_rules[] = {
        [QA_GAME_Q1] = QA_RULESET_NETQUAKE, [QA_GAME_Q2] = QA_RULESET_Q2_CLASSIC, [QA_GAME_Q3] = QA_RULESET_Q3
    };
    return source_rules[family];
}

void qa_collision_pose_basis(const qa_collision_target *target, bool transformed, qa_vec3 basis[3])
{
    qa_collision_basis(transformed && qa_collision_roles[target->pose_rules].rotates
        ? target->angles : qa_v3(0, 0, 0), basis);
}

qa_vec3 qa_collision_pose_normal(qa_vec3 normal, const qa_collision_target *target, bool transformed, const qa_vec3 basis[3])
{
    if (transformed && qa_collision_roles[target->pose_rules].inverse_normal) {
        qa_vec3 inverse[3];
        qa_collision_basis(qa_vec_scale(target->angles, -1), inverse);
        return qa_collision_to_local(normal, inverse);
    }
    return qa_collision_from_local(normal, basis);
}

qa_bounds qa_collision_link_bounds(qa_bounds bounds, qa_vec3 origin, qa_vec3 angles,
    bool rotated_brush, qa_vec3 padding, qa_ruleset_id rules)
{
    const qa_collision_role_rules *role = &qa_collision_roles[rules];
    if (rotated_brush && role->rotates &&
        (angles.x != 0 || angles.y != 0 || angles.z != 0)) {
        qa_vec3 extent = qa_v3(fmaxf(fabsf(bounds.mins.x), fabsf(bounds.maxs.x)),
            fmaxf(fabsf(bounds.mins.y), fabsf(bounds.maxs.y)),
            fmaxf(fabsf(bounds.mins.z), fabsf(bounds.maxs.z)));
        float radius = role->sphere_bounds ? qa_vec_length(extent)
            : fmaxf(extent.x, fmaxf(extent.y, extent.z));
        qa_vec3 offset = qa_v3(radius, radius, radius);
        bounds = (qa_bounds){qa_vec_sub(origin, offset), qa_vec_add(origin, offset)};
    } else bounds = qa_bounds_translate(bounds, origin);
    bounds.mins = qa_vec_sub(bounds.mins, padding);
    bounds.maxs = qa_vec_add(bounds.maxs, padding);
    return bounds;
}

qa_trace_policy qa_collision_default_policy(qa_game_family family)
{
    if ((unsigned)family > QA_GAME_Q3) return (qa_trace_policy){0};
    return (qa_trace_policy){.behavior = &qa_trace_behaviors[qa_collision_source_rules(family)],
        .contents_mask=qa_collision_contents_mask(UINT32_MAX,family),
        .q1_move=QA_Q1_MOVE_NORMAL,.q1_hull=-1,
        .curves=true,.player_curve_clip=true};
}

/* Canonical fields stay in one domain. Only the caller's contact conventions
 * differ: Q1's axial plane, Q2's named surface and rerelease second plane. */
void qa_collision_adapt_trace(qa_trace_result *result,const qa_trace_policy *policy)
{
    qa_game_family from=result->family,to=policy->behavior->contents_format;
    if(from==to) return;
    bool sky=from==QA_GAME_Q1 && (
        qa_collision_bits_overlap(result->contents,qa_collision_bit(QA_CONTENT_SKY)) ||
        qa_collision_bits_overlap(result->surface_flags,qa_collision_bit(QA_SURFACE_SKY_NOIMPACT)));
    qa_collision_bits flags=from==QA_GAME_Q2 ?
        (result->has_surface?result->surface.flags:(qa_collision_bits){0}) : result->surface_flags;
    if(from==QA_GAME_Q1) {
        qa_vec3 normal=result->contact?result->contact_plane.normal:result->plane.normal;
        int32_t type=normal.x==1.0f?0:normal.y==1.0f?1:normal.z==1.0f?2:3;
        result->plane=qa_collision_make_plane(normal,result->plane.distance,type);
    }
    result->family=to;
    if(to!=QA_GAME_Q2 || !policy->behavior->merged_contents) {
        result->has_secondary=false; result->secondary_has_surface=false;
    }
    if(to==QA_GAME_Q1) {
        result->surface_flags=flags;
        result->has_surface=false;
    } else if(to==QA_GAME_Q2) {
        memset(&result->surface,0,sizeof(result->surface));
        result->has_surface=from==QA_GAME_Q3 || sky || result->hit!=QA_TRACE_HIT_NONE;
        result->surface.flags=from==QA_GAME_Q3?flags:
            sky?qa_collision_bit(QA_SURFACE_SKY_NOIMPACT):(qa_collision_bits){0};
        if(sky) memcpy(result->surface.name,"sky",4);
        result->surface_flags=result->surface.flags;
        if(result->has_secondary) {
            result->secondary_has_surface=result->has_surface;
            result->secondary_surface=result->surface;
        }
    } else {
        result->has_surface=false;
        result->surface_flags=from==QA_GAME_Q2?flags:
            sky?qa_collision_bit(QA_SURFACE_SKY_NOIMPACT):(qa_collision_bits){0};
    }
}

void qa_collision_adapt_point(qa_point_contents *result,const qa_trace_policy *policy)
{
    if(result->family==policy->behavior->contents_format) return;
    qa_collision_bits contents=result->family==QA_GAME_Q2?result->merged:result->contents;
    result->family=policy->behavior->contents_format;
    result->contents=result->stored=result->merged=contents;
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

static bool trace_q2_box(const qa_trace_query *query,qa_bounds target,qa_vec3 origin,qa_collision_bits contents,qa_trace_result *out)
{
    qa_bounds moving=query->shape.kind==QA_SHAPE_POINT?(qa_bounds){0}:query->shape.bounds;
    q2_box_trace box={{qa_vec_sub(target.mins,moving.maxs),qa_vec_sub(target.maxs,moving.mins)},
        qa_vec_sub(query->start,origin),qa_vec_sub(query->end,origin)};
    qa_trace_result result=qa_collision_empty_trace(query,QA_GAME_Q2);
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

bool qa_collision_trace_body(const qa_trace_query *query,qa_game_family actor_family,qa_shape_kind target_kind,qa_bounds target,qa_vec3 origin,qa_collision_bits contents,qa_trace_result *out,qa_error *error)
{
    if(query->policy.behavior->hull_boxes && query->shape.kind!=QA_SHAPE_CAPSULE && target_kind!=QA_SHAPE_CAPSULE)
        return qa_q1_trace_box(query,target,origin,out,error);
    if(query->policy.behavior->legacy_boxes && query->shape.kind!=QA_SHAPE_CAPSULE && target_kind!=QA_SHAPE_CAPSULE)
        return trace_q2_box(query,target,origin,contents,out);
    bool native_q3=query->policy.behavior->owner_pairs && actor_family==QA_GAME_Q3;
    qa_trace_query local=*query;
    local.target=(qa_collision_target){0};
    local.policy.contents_mask=native_q3?query->policy.contents_mask:qa_collision_bit(QA_CONTENT_BODY);
    if(!qa_q3_trace_shape(&local,target_kind,target,origin,qa_collision_bit(QA_CONTENT_BODY),out,error)) return false;
    if(!native_q3) {
        bool occupied=out->fraction<1 || (query->policy.behavior->hull_boxes && out->start_solid);
        out->contents=occupied?contents:(qa_collision_bits){0};
    }
    if(query->policy.behavior->hull_boxes) { out->in_open=!out->all_solid; out->in_water=qa_collision_bits_overlap(out->contents,(qa_collision_bits){56,0}); }
    qa_collision_adapt_trace(out,&query->policy); return true;
}
