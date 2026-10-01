#include "internal.h"
#include <limits.h>
#include <stdlib.h>

static bool valid_kind(qa_movement_kind kind) {
    return kind >= QA_MOVEMENT_NETQUAKE && kind <= QA_MOVEMENT_Q3;
}
static bool fail(qa_move_context *c, const char *message) {
    c->failed = true;
    qa_error_set(c->error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool valid_bounds(qa_bounds b) {
    return qa_vec_finite(b.mins) && qa_vec_finite(b.maxs) &&
        b.mins.x <= b.maxs.x && b.mins.y <= b.maxs.y && b.mins.z <= b.maxs.z;
}
static bool valid_origin(const qa_movement_state *state) {
    if (state->kind == QA_MOVEMENT_QUAKEWORLD) {
        qa_qw_origin value = state->data.qw.origin;
        return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
    }
    return qa_vec_finite(qa_movement_origin(state));
}
static qa_collision_family family(qa_movement_kind kind) {
    return kind <= QA_MOVEMENT_QUAKEWORLD ? QA_COLLISION_Q1 :
        kind == QA_MOVEMENT_Q3 ? QA_COLLISION_Q3 : QA_COLLISION_Q2;
}

float qa_move_component(qa_vec3 v, unsigned axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
void qa_move_set_component(qa_vec3 *v, unsigned axis, float x) {
    if (axis == 0) v->x = x; else if (axis == 1) v->y = x; else v->z = x;
}
int16_t qa_move_short(int32_t value) {
    uint32_t word = (uint32_t)value & UINT32_C(65535);
    return (int16_t)(word >= 32768 ? (int32_t)word - 65536 : (int32_t)word);
}
float qa_move_short_angle(int32_t word) { return (float)qa_move_short(word) * (360.0f / 65536.0f); }
float qa_move_angle_mod(float value) {
    float word = truncf(fmodf(value, 360.0f) * (65536.0f / 360.0f));
    if (!isfinite(word)) return 0;
    return (float)((uint32_t)(int32_t)word & 65535u) * (360.0f / 65536.0f);
}
void qa_move_angles(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up) {
    const float radians = 0.01745329251994329577f;
    float sy = sinf(angles.y*radians), cy = cosf(angles.y*radians);
    float sp = sinf(angles.x*radians), cp = cosf(angles.x*radians);
    float sr = sinf(angles.z*radians), cr = cosf(angles.z*radians);
    if (forward) *forward = qa_v3(cp*cy, cp*sy, -sp);
    if (right) *right = qa_v3(-sr*sp*cy+cr*sy, -sr*sp*sy-cr*cy, -sr*cp);
    if (up) *up = qa_v3(cr*sp*cy+sr*sy, cr*sp*sy-sr*cy, cr*cp);
}
qa_vec3 qa_move_clip(qa_vec3 velocity, qa_vec3 normal, float overbounce, float epsilon) {
    float backoff = qa_vec_dot(velocity, normal) * overbounce;
    qa_vec3 out = qa_vec_sub(velocity, qa_vec_scale(normal, backoff));
    if (fabsf(out.x) < epsilon) out.x = 0;
    if (fabsf(out.y) < epsilon) out.y = 0;
    if (fabsf(out.z) < epsilon) out.z = 0;
    return out;
}

qa_movement_profile qa_movement_profile_default(qa_movement_kind kind) {
    qa_movement_profile p = {.kind=kind};
    qa_q1_movement_parameters q1 = {800,100,320,500,10,10,10,4,4,1};
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE:
        p.data.nq.parameters=q1; p.data.nq.edition=QA_Q1_CLASSIC;
        p.data.nq.edge_friction=2; p.data.nq.max_velocity=2000;
        p.data.nq.ideal_pitch_scale=0.8f; p.data.nq.roll_speed=200;
        p.data.nq.roll_angle=2; p.data.nq.preserve_fixangle_roll=false;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        q1.air_accelerate=0.7f; p.data.qw.parameters=q1;
        p.data.qw.maximum_command_ms=50; break;
    case QA_MOVEMENT_Q2_CLASSIC: p.data.q2.air_accelerate=0; break;
    case QA_MOVEMENT_Q2_RERELEASE: p.data.q2r.air_accelerate=0; break;
    case QA_MOVEMENT_Q3: break;
    }
    return p;
}
qa_movement_environment qa_movement_environment_default(void) {
    return (qa_movement_environment){.health=100,.gravity_multiplier=1,.speed_multiplier=1};
}
qa_movement_state qa_movement_state_default(qa_movement_kind kind, qa_vec3 origin) {
    qa_movement_state s = {.kind=kind};
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE:
        s.data.nq.origin=origin; s.data.nq.old_origin=origin;
        s.data.nq.move_type=3; s.data.nq.health=100; s.data.nq.flags=4096;
        s.data.nq.water_type=-1; break;
    case QA_MOVEMENT_QUAKEWORLD: s.data.qw.origin=qa_qw_origin_from_vec3(origin); break;
    case QA_MOVEMENT_Q2_CLASSIC:
        s.data.q2.gravity=800; (void)qa_movement_set_origin(&s,origin,NULL); break;
    case QA_MOVEMENT_Q2_RERELEASE:
        s.data.q2r.origin=origin; s.data.q2r.gravity=800; s.data.q2r.view_height=22; break;
    case QA_MOVEMENT_Q3:
        s.data.q3.origin=origin; s.data.q3.gravity=800; s.data.q3.speed=320;
        s.data.q3.view_height=26; break;
    }
    return s;
}
qa_movement_input qa_movement_input_default(qa_movement_kind kind, qa_actor_id actor) {
    qa_movement_input in = {.actor=actor,.state=qa_movement_state_default(kind,qa_v3(0,0,0)),
        .command={.kind=kind},.profile=qa_movement_profile_default(kind),
        .environment=qa_movement_environment_default(),.q1_solid=QA_Q1_SOLID_SLIDEBOX};
    float radius=kind==QA_MOVEMENT_Q3?15.0f:16.0f;
    in.standing=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,32)},kind==QA_MOVEMENT_Q3?26.0f:22.0f};
    in.crouched=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,kind==QA_MOVEMENT_Q3?16.0f:4.0f)},kind==QA_MOVEMENT_Q3?12.0f:-2.0f};
    in.dead=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,-8)},-16};
    in.invulnerability_bounds=(qa_bounds){qa_v3(-42,-42,-42),qa_v3(42,42,42)};
    in.shape=(qa_trace_shape){QA_SHAPE_BOX,in.standing.bounds};
    in.current_bounds=in.shape.bounds;
    in.trace_policy=qa_collision_default_policy(family(kind));
    return in;
}
qa_vec3 qa_movement_origin(const qa_movement_state *s) {
    if (!s) return qa_v3(0,0,0);
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE: return s->data.nq.origin;
    case QA_MOVEMENT_QUAKEWORLD: return qa_qw_origin_to_vec3(s->data.qw.origin);
    case QA_MOVEMENT_Q2_CLASSIC: return qa_v3(s->data.q2.origin_eighths[0]*0.125f,s->data.q2.origin_eighths[1]*0.125f,s->data.q2.origin_eighths[2]*0.125f);
    case QA_MOVEMENT_Q2_RERELEASE: return s->data.q2r.origin;
    case QA_MOVEMENT_Q3: return s->data.q3.origin;
    }
    return qa_v3(0,0,0);
}
qa_vec3 qa_movement_velocity(const qa_movement_state *s) {
    if (!s) return qa_v3(0,0,0);
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE: return s->data.nq.velocity;
    case QA_MOVEMENT_QUAKEWORLD: return s->data.qw.velocity;
    case QA_MOVEMENT_Q2_CLASSIC: return qa_v3(s->data.q2.velocity_eighths[0]*0.125f,s->data.q2.velocity_eighths[1]*0.125f,s->data.q2.velocity_eighths[2]*0.125f);
    case QA_MOVEMENT_Q2_RERELEASE: return s->data.q2r.velocity;
    case QA_MOVEMENT_Q3: return s->data.q3.velocity;
    }
    return qa_v3(0,0,0);
}
static bool write_vector(qa_movement_state *s, qa_vec3 value, bool velocity, qa_error *error) {
    if (!s || !valid_kind(s->kind) || !qa_vec_finite(value)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid movement vector or family"); return false;
    }
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE:
        if (velocity) s->data.nq.velocity=value; else s->data.nq.origin=value; break;
    case QA_MOVEMENT_QUAKEWORLD:
        if (velocity) s->data.qw.velocity=value; else s->data.qw.origin=qa_qw_origin_from_vec3(value); break;
    case QA_MOVEMENT_Q2_CLASSIC: {
        int16_t words[3];
        for (unsigned axis=0;axis<3;axis++) {
            float scaled=qa_move_component(value,axis)*8.0f;
            if (!isfinite(scaled)) { qa_error_set(error,QA_ERROR_ARGUMENT,axis,"Movement eighth conversion overflow"); return false; }
            words[axis]=qa_move_short((int32_t)fmodf(truncf(scaled),65536.0f));
        }
        memcpy(velocity?s->data.q2.velocity_eighths:s->data.q2.origin_eighths,words,sizeof(words)); break;
    }
    case QA_MOVEMENT_Q2_RERELEASE:
        if (velocity) s->data.q2r.velocity=value; else s->data.q2r.origin=value; break;
    case QA_MOVEMENT_Q3:
        if (velocity) s->data.q3.velocity=value; else s->data.q3.origin=value; break;
    }
    return true;
}
bool qa_movement_set_origin(qa_movement_state *s, qa_vec3 v, qa_error *e) { return write_vector(s,v,false,e); }
bool qa_movement_set_velocity(qa_movement_state *s, qa_vec3 v, qa_error *e) { return write_vector(s,v,true,e); }

qa_movement_ground qa_move_ground(const qa_trace_result *t) {
    return (qa_movement_ground){t->hit,t->actor,t->model};
}
bool qa_move_same_ground(qa_movement_ground a, qa_movement_ground b) {
    return a.hit==b.hit && (a.hit==QA_TRACE_HIT_NONE ||
        (a.hit==QA_TRACE_HIT_WORLD?a.model==b.model:qa_actor_id_equal(a.actor,b.actor)));
}
static qa_trace_policy policy(qa_move_context *c, uint32_t mask) {
    qa_collision_family f=family(c->state->kind);
    qa_trace_policy p=c->input->has_trace_policy?c->input->trace_policy:qa_collision_default_policy(f);
    p.family=f; p.contents_mask=mask;
    if (f==QA_COLLISION_Q2) p.q2_merged_contents=c->state->kind==QA_MOVEMENT_Q2_RERELEASE;
    return p;
}
static bool trace_query(qa_move_context *c, const qa_trace_query *q, qa_trace_result *out) {
    if (c->failed||c->removed) return false;
    if (!c->services->trace(c->services->context,q,out,c->error)) { c->failed=true; return false; }
    if (out->family!=q->policy.family || !isfinite(out->fraction) || out->fraction<0 || out->fraction>1 || !qa_vec_finite(out->end))
        return fail(c,"Movement trace returned an invalid family, fraction or position");
    return true;
}
bool qa_move_trace(qa_move_context *c, qa_vec3 start, qa_vec3 end, qa_bounds bounds, uint32_t mask, bool world_only, qa_trace_result *out) {
    qa_trace_query q={.start=start,.end=end,.shape={c->input->shape.kind,bounds},
        .policy=policy(c,mask),.pass_actor=c->input->actor};
    if (c->input->environment.fixed_pose) q.shape.kind=QA_SHAPE_BOX;
    if (world_only) { q.target.inline_model=true; q.target.model=0; }
    return trace_query(c,&q,out);
}
bool qa_move_trace_q1(qa_move_context *c, qa_vec3 start, qa_vec3 end, qa_trace_shape shape, qa_q1_move_kind move, qa_trace_result *out) {
    qa_trace_query q={.start=start,.end=end,.shape=shape,.policy=policy(c,UINT32_MAX),.pass_actor=c->input->actor};
    q.policy.q1_move=move; q.policy.q1_hull=-1;
    return trace_query(c,&q,out);
}
bool qa_move_contents(qa_move_context *c, qa_vec3 point, int32_t *out) {
    if (c->failed||c->removed) return false;
    qa_point_query q={.point=point,.policy=policy(c,UINT32_MAX),.pass_actor=c->input->actor};
    qa_point_contents result;
    if (!c->services->point_contents(c->services->context,&q,&result,c->error)) { c->failed=true; return false; }
    if (result.family!=q.policy.family) return fail(c,"Movement contents returned another collision family");
    *out=result.family==QA_COLLISION_Q2?(q.policy.q2_merged_contents?result.merged:result.stored):result.contents;
    if (result.family==QA_COLLISION_Q1 && *out<=-9 && *out>=-14) *out=-3;
    return true;
}
static qa_movement_call call(qa_move_context *c) {
    return (qa_movement_call){.actor=c->input->actor,.state=c->state,.command=&c->command,
        .bounds=&c->result->bounds,.view_height=c->state->kind==QA_MOVEMENT_Q3?&c->state->data.q3.view_height:
            c->state->kind==QA_MOVEMENT_Q2_RERELEASE?&c->state->data.q2r.view_height:&c->result->view_height,
        .water_level=&c->result->water_level,.water_type=&c->result->water_type,
        .time_ns=c->time_ns,.milliseconds=c->milliseconds,.substep=c->substep,.elapsed_seconds=c->dt,.prediction=c->input->prediction,
        .source_time_ms=c->command.server_time_ms,.profile=&c->input->profile,.environment=&c->input->environment};
}
static bool control(qa_move_context *c, qa_movement_control result) {
    if (result==QA_MOVEMENT_ERROR) { c->failed=true; return false; }
    if (result==QA_MOVEMENT_REMOVED) { c->removed=true; c->result->status=QA_MOVEMENT_ACTOR_REMOVED; return false; }
    if (result!=QA_MOVEMENT_CONTINUE) return fail(c,"Movement callback returned an invalid continuation");
    if (c->state->kind!=c->input->profile.kind || c->command.kind!=c->input->profile.kind)
        return fail(c,"Movement callback changed the selected family inside a command");
    return !c->failed&&!c->removed;
}
bool qa_move_phase(qa_move_context *c, qa_movement_phase phase) {
    c->phase_state_replaced=false;
    bool input=phase==QA_MOVE_INPUT_BEGIN||phase==QA_MOVE_INPUT_END||phase==QA_MOVE_INPUT_ABORT;
    bool closing=phase==QA_MOVE_INPUT_END||phase==QA_MOVE_INPUT_ABORT;
    if (input&&c->input->prediction) return !c->failed&&!c->removed;
    if ((c->failed||c->removed)&&!closing) return false;
    if (phase==QA_MOVE_INPUT_BEGIN) c->input_open=true;
    if (closing) c->input_open=false;
    if (!c->services->phase) return !c->failed&&!c->removed;
    qa_movement_call invocation=call(c);
    qa_movement_control result=c->services->phase(c->services->context,phase,&invocation,c->error);
    c->phase_state_replaced=invocation.state_replaced;
    return control(c,result);
}
bool qa_move_firing(qa_move_context *c) {
    if (!c->services->firing||c->failed||c->removed) return false;
    qa_movement_call invocation=call(c);
    return c->services->firing(c->services->context,&invocation);
}
bool qa_move_emit(qa_move_context *c, qa_movement_effect effect) {
    if (c->failed||c->removed) return false;
    effect.sequence=c->result->effect_count++; effect.time_ns=c->time_ns; effect.substep=c->substep;
    effect.source_time_ms=c->command.server_time_ms;
    if (!c->services->effect) return true;
    qa_movement_call invocation=call(c);
    return control(c,c->services->effect(c->services->context,&effect,&invocation,c->error));
}
bool qa_move_event(qa_move_context *c, int32_t event, int32_t parameter) {
    uint32_t sequence=0;
    if (c->state->kind==QA_MOVEMENT_Q3) sequence=c->state->data.q3.event_sequence++;
    return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_EVENT,.event_sequence=sequence,.value=event,.parameter=parameter});
}
bool qa_move_animation(qa_move_context *c, qa_movement_animation_kind kind, int32_t value, bool force, bool backwards) {
    return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_ANIMATION,.animation_kind=kind,.value=value,.force=force,.backwards=backwards});
}
bool qa_move_touch(qa_move_context *c, const qa_trace_result *trace) {
    if (trace->hit==QA_TRACE_HIT_NONE) return !c->failed&&!c->removed;
    if (!qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_TOUCH,.trace=trace})) return false;
    if (!c->services->touch) return true;
    qa_movement_call invocation=call(c);
    return control(c,c->services->touch(c->services->context,trace,&invocation,c->error));
}
bool qa_move_contact(qa_move_context *c, const qa_trace_result *trace, bool touch_now, bool unique) {
    if (c->failed||c->removed) return false;
    if (trace->hit==QA_TRACE_HIT_NONE&&(c->state->kind!=QA_MOVEMENT_QUAKEWORLD||touch_now)) return true;
    qa_movement_result *r=c->result;
    if (unique) for (size_t i=0;i<r->contact_count;i++)
        if (qa_move_same_ground(qa_move_ground(&r->contacts[i].trace),qa_move_ground(trace))) return true;
    if (r->contact_count==r->contact_capacity) {
        size_t capacity=r->contact_capacity?r->contact_capacity*2:32;
        if (capacity<r->contact_capacity||capacity>SIZE_MAX/sizeof(*r->contacts)) {
            qa_error_set(c->error,QA_ERROR_MEMORY,0,"Movement contact capacity overflow"); c->failed=true; return false;
        }
        qa_movement_contact *contacts=realloc(r->contacts,capacity*sizeof(*contacts));
        if (!contacts) { qa_error_set(c->error,QA_ERROR_MEMORY,0,"Allocating movement contacts"); c->failed=true; return false; }
        r->contacts=contacts; r->contact_capacity=capacity;
    }
    r->contacts[r->contact_count++]=(qa_movement_contact){*trace,c->substep};
    if (touch_now) return qa_move_touch(c,trace);
    if (c->state->kind==QA_MOVEMENT_Q3) return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_TOUCH,.trace=trace});
    return true;
}
bool qa_move_bounds(qa_move_context *c, qa_bounds requested, uint32_t mask, qa_bounds *out) {
    qa_bounds previous=c->result->bounds;
    bool expands=requested.mins.x<previous.mins.x||requested.mins.y<previous.mins.y||requested.mins.z<previous.mins.z||
        requested.maxs.x>previous.maxs.x||requested.maxs.y>previous.maxs.y||requested.maxs.z>previous.maxs.z;
    if (!valid_bounds(requested)) return fail(c,"Invalid requested movement bounds");
    if (expands) {
        qa_vec3 origin=qa_movement_origin(c->state); qa_trace_result trace;
        if (!qa_move_trace(c,origin,origin,requested,mask,false,&trace)) return false;
        if (trace.all_solid||(c->state->kind!=QA_MOVEMENT_Q3&&trace.start_solid)) { *out=previous; return true; }
    }
    *out=requested; return true;
}
int32_t qa_move_mode_type(qa_movement_kind kind, qa_movement_mode mode) {
    if (kind==QA_MOVEMENT_NETQUAKE) return mode==QA_MOVEMENT_MODE_NORMAL?3:mode==QA_MOVEMENT_MODE_NOCLIP?8:0;
    if (kind==QA_MOVEMENT_QUAKEWORLD) return mode==QA_MOVEMENT_MODE_NOCLIP?1:0;
    if (kind==QA_MOVEMENT_Q2_RERELEASE) return mode==QA_MOVEMENT_MODE_NORMAL?0:mode==QA_MOVEMENT_MODE_NOCLIP?2:6;
    return mode==QA_MOVEMENT_MODE_NORMAL?0:mode==QA_MOVEMENT_MODE_NOCLIP?1:4;
}
bool qa_move_apply_stance(qa_move_context *c) {
    const qa_movement_environment *e=&c->input->environment;
    if (!e->has_stance) return true;
    if (c->state->kind==QA_MOVEMENT_NETQUAKE) return fail(c,"NetQuake has no source crouch command");
    if (c->state->kind==QA_MOVEMENT_Q2_RERELEASE) {
        if (e->crouched) c->command.buttons|=16u; else c->command.buttons&=~16u;
    } else {
        float up=fabsf(c->command.up_move);
        if (up==0) up=1;
        if (e->crouched) c->command.up_move=-up;
        else if (c->command.up_move<0) c->command.up_move=0;
        if (c->state->kind==QA_MOVEMENT_QUAKEWORLD&&e->crouched) c->command.buttons&=~2u;
    }
    return true;
}

bool qa_movement_world_trace(void *world, const qa_trace_query *q, qa_trace_result *r, qa_error *e) { return qa_world_trace(world,q,r,e); }
bool qa_movement_world_contents(void *world, const qa_point_query *q, qa_point_contents *r, qa_error *e) { return qa_world_point_contents(world,q,r,e); }
void qa_movement_result_free(qa_movement_result *r) { if (r) { free(r->contacts); memset(r,0,sizeof(*r)); } }

static bool move_stage(const qa_movement_input *input, const qa_movement_services *services, qa_movement_result *out, qa_error *error, unsigned stage) {
    if (!input||!services||!out||!services->trace||!services->point_contents||
        !valid_kind(input->profile.kind)||(stage&&input->profile.kind!=QA_MOVEMENT_NETQUAKE)||
        input->state.kind!=input->profile.kind||input->command.kind!=input->profile.kind||
        !isfinite(input->command.forward_move)||!isfinite(input->command.side_move)||!isfinite(input->command.up_move)||
        (input->state.kind!=QA_MOVEMENT_NETQUAKE&&
         (!valid_origin(&input->state)||!qa_vec_finite(qa_movement_velocity(&input->state))))||
        input->shape.kind<QA_SHAPE_POINT||input->shape.kind>QA_SHAPE_CAPSULE||
        (input->shape.kind!=QA_SHAPE_POINT&&!valid_bounds(input->shape.bounds))||
        (input->has_current_bounds&&!valid_bounds(input->current_bounds))||
        (input->environment.has_body_bounds&&!valid_bounds(input->environment.body_bounds))||
        (input->environment.fixed_pose&&(!valid_bounds(input->environment.pose.bounds)||!isfinite(input->environment.pose.view_height)))||
        !valid_bounds(input->standing.bounds)||!isfinite(input->standing.view_height)||
        !valid_bounds(input->crouched.bounds)||!isfinite(input->crouched.view_height)||
        !valid_bounds(input->dead.bounds)||!isfinite(input->dead.view_height)||
        !isfinite(input->environment.gravity_multiplier)||input->environment.gravity_multiplier<0||
        !isfinite(input->environment.speed_multiplier)||input->environment.speed_multiplier<0) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid movement input, selected family or services"); return false;
    }
    qa_movement_result result={.contacts=out->contacts,.contact_capacity=out->contact_capacity,
        .actor=input->actor,.command_sequence=input->command.sequence,.state=input->state,
        .bounds=input->has_current_bounds?input->current_bounds:input->shape.bounds,.view_height=input->standing.view_height,.view_offset=input->view_offset};
    if (input->shape.kind==QA_SHAPE_POINT) result.bounds=(qa_bounds){0};
    qa_movement_input active_input=*input;
    qa_move_context c={.input=&active_input,.services=services,.result=&result,.state=&result.state,.command=input->command,.error=error,
        .milliseconds=input->command.milliseconds,.time_ns=input->time_ns,.dt=(float)((double)input->elapsed_ns/1000000000.0)};
    bool q2=input->profile.kind==QA_MOVEMENT_Q2_CLASSIC||input->profile.kind==QA_MOVEMENT_Q2_RERELEASE;
    if (q2) {
        if (c.milliseconds>255) fail(&c,"Q2 command duration must fit its source byte");
        c.dt=(float)c.milliseconds*0.001f;
        if (!c.failed) (void)qa_move_phase(&c,QA_MOVE_INPUT_BEGIN);
        c.milliseconds=c.command.milliseconds; c.dt=(float)c.milliseconds*0.001f;
        if (!c.failed&&!c.removed&&c.milliseconds>255) fail(&c,"Q2 input callback returned an invalid command duration");
        if (!c.failed&&!c.removed) (void)qa_move_apply_stance(&c);
    }
    bool ok=true;
    if (!c.failed&&!c.removed) switch (input->profile.kind) {
    case QA_MOVEMENT_NETQUAKE: ok=stage==1?qa_move_nq_prepare(&c):stage==2?qa_move_nq_physics(&c):qa_move_nq(&c); break;
    case QA_MOVEMENT_QUAKEWORLD: ok=qa_move_qw(&c); break;
    case QA_MOVEMENT_Q2_CLASSIC: ok=qa_move_q2(&c); break;
    case QA_MOVEMENT_Q2_RERELEASE: ok=qa_move_q2r(&c); break;
    case QA_MOVEMENT_Q3: ok=qa_move_q3(&c); break;
    }
    if (!ok&&!c.removed) c.failed=true;
    if (c.input_open) (void)qa_move_phase(&c,c.failed?QA_MOVE_INPUT_ABORT:QA_MOVE_INPUT_END);
    if (c.failed) {
        out->contacts=result.contacts; out->contact_capacity=result.contact_capacity;
        out->contact_count=0;
        return false;
    }
    result.status=c.removed?QA_MOVEMENT_ACTOR_REMOVED:QA_MOVEMENT_ACTIVE;
    *out=result; return true;
}

bool qa_movement_move(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,0);
}
bool qa_movement_prepare_netquake(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,1);
}
bool qa_movement_physics_netquake(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,2);
}
bool qa_movement_apply_q2_contacts(const qa_movement_input *input, const qa_movement_services *services, qa_movement_result *result, qa_error *error) {
    if (!input||!services||!result||!qa_actor_id_equal(input->actor,result->actor)||
        (input->profile.kind!=QA_MOVEMENT_Q2_CLASSIC&&input->profile.kind!=QA_MOVEMENT_Q2_RERELEASE)||result->state.kind!=input->profile.kind) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 contact result belongs to another player or movement family"); return false;
    }
    if (result->status==QA_MOVEMENT_ACTOR_REMOVED) return true;
    qa_movement_input active_input=*input;
    qa_move_context c={.input=&active_input,.services=services,.result=result,.state=&result->state,.command=input->command,
        .error=error,.milliseconds=input->command.milliseconds,.time_ns=input->time_ns,.dt=(float)input->command.milliseconds*0.001f};
    for (size_t i=0;i<result->contact_count&&!c.removed&&!c.failed;i++) {
        c.substep=result->contacts[i].substep;
        (void)qa_move_touch(&c,&result->contacts[i].trace);
    }
    return !c.failed;
}
void qa_movement_q3_jump_pad(qa_movement_state *state, qa_actor_id pad, qa_vec3 velocity, bool flight, int32_t *event, int32_t *parameter) {
    if (event) *event=0;
    if (parameter) *parameter=0;
    if (!state||state->kind!=QA_MOVEMENT_Q3||state->data.q3.movement_type!=0||flight) return;
    qa_q3_movement_state *s=&state->data.q3;
    bool changed=s->jump_pad.registry==0||!qa_actor_id_equal(s->jump_pad,pad);
    s->jump_pad=pad; s->jump_pad_frame=s->movement_frame; s->velocity=velocity;
    if (changed) {
        float pitch=velocity.x==0&&velocity.y==0?(velocity.z>0?-90.0f:-270.0f):
            -atan2f(velocity.z,hypotf(velocity.x,velocity.y))*57.29577951308232f;
        pitch=qa_move_angle_mod(pitch);
        pitch=fabsf(pitch>180?pitch-360:pitch);
        s->event_sequence++;
        if (event) *event=13;
        if (parameter) *parameter=pitch<45?0:1;
    }
}
