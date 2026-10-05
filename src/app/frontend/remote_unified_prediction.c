#include "remote_unified_prediction_private.h"
#include "remote_unified_save.h"
#include "qa/text.h"
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct prediction_reader {
    const qa_unified_document *document;
    const qa_json_document *json;
    frontend_remote_unified *replica;
    bool importing;
} prediction_reader;

static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e, code, 0, "%s", message); return false; }
static qa_json_id get(const prediction_reader *r, qa_json_id id, const char *name)
{ return qa_json_get(r->json, id, name); }
static bool equal(const prediction_reader *r, qa_json_id id, const char *value)
{ return qa_json_string_equal(r->json, id, value); }
static bool number(const prediction_reader *r, qa_json_id id, double *out, qa_error *e)
{
    double v;
    if (!qa_unified_document_number(r->document, id, &v, e)) return false;
    if (!isfinite(v)) return fail(e, QA_ERROR_FORMAT, "Unified prediction requires a finite native scalar");
    *out = v; return true;
}
static bool real(const prediction_reader *r, qa_json_id id, float *out, qa_error *e)
{
    double v;
    if (!number(r, id, &v, e)) return false;
    if (fabs(v) > FLT_MAX) return fail(e, QA_ERROR_FORMAT, "Unified prediction scalar exceeds its native float field");
    *out = (float)v; return true;
}
static bool integer(const prediction_reader *r, qa_json_id id, double lo, double hi, int64_t *out, qa_error *e)
{
    double v;
    if (!number(r, id, &v, e)) return false;
    if (trunc(v) != v || v < lo || v > hi) return fail(e, QA_ERROR_FORMAT, "Unified prediction integer exceeds its native field");
    *out = (int64_t)v; return true;
}
static bool i32(const prediction_reader *r, qa_json_id id, int32_t *out, qa_error *e)
{ int64_t v; if (!integer(r, id, INT32_MIN, INT32_MAX, &v, e)) return false; *out = (int32_t)v; return true; }
static bool u32(const prediction_reader *r, qa_json_id id, uint32_t *out, qa_error *e)
{ int64_t v; if (!integer(r, id, 0, UINT32_MAX, &v, e)) return false; *out = (uint32_t)v; return true; }
static bool i16(const prediction_reader *r, qa_json_id id, int16_t *out, qa_error *e)
{ int64_t v; if (!integer(r, id, INT16_MIN, INT16_MAX, &v, e)) return false; *out = (int16_t)v; return true; }
static bool u8(const prediction_reader *r, qa_json_id id, uint8_t *out, qa_error *e)
{ int64_t v; if (!integer(r, id, 0, UINT8_MAX, &v, e)) return false; *out = (uint8_t)v; return true; }
static bool u16(const prediction_reader *r, qa_json_id id, uint16_t *out, qa_error *e)
{ int64_t v; if (!integer(r, id, 0, UINT16_MAX, &v, e)) return false; *out = (uint16_t)v; return true; }
static bool boolean(const prediction_reader *r, qa_json_id id, bool *out, qa_error *e)
{ return qa_json_bool(r->json, id, out, e); }
static bool vector(const prediction_reader *r, qa_json_id id, qa_vec3 *out, qa_error *e)
{
    return real(r, get(r, id, "x"), &out->x, e) && real(r, get(r, id, "y"), &out->y, e) &&
        real(r, get(r, id, "z"), &out->z, e);
}
static bool bounds(const prediction_reader *r, qa_json_id id, qa_bounds *out, qa_error *e)
{
    if (!vector(r, get(r, id, "min"), &out->mins, e) || !vector(r, get(r, id, "max"), &out->maxs, e)) return false;
    return (out->mins.x<=out->maxs.x && out->mins.y<=out->maxs.y && out->mins.z<=out->maxs.z) ||
        fail(e, QA_ERROR_FORMAT, "Unified prediction bounds are inverted");
}
static bool actor(const prediction_reader *r, qa_json_id id, qa_actor_id *out, qa_error *e)
{
    if (qa_json_type(r->json, id) == QA_JSON_NULL) { *out = (qa_actor_id){0}; return true; }
    uint32_t slot; int64_t generation;
    if (!u32(r, get(r, id, "slot"), &slot, e) ||
        !integer(r, get(r, id, "generation"), 0, QA_UNIFIED_SAFE_INTEGER, &generation, e)) return false;
    return r->importing ? frontend_remote_unified_actor_retained(r->replica,slot,(uint64_t)generation,out,e) :
        frontend_remote_unified_actor(r->replica,slot,(uint64_t)generation,out,e);
}
static bool ground(const prediction_reader *r, qa_json_id id, qa_movement_ground *out, qa_error *e)
{
    qa_json_id kind = get(r, id, "kind");
    *out = (qa_movement_ground){0};
    if (equal(r, kind, "none")) { out->hit = QA_TRACE_HIT_NONE; return true; }
    if (equal(r, kind, "world")) { out->hit = QA_TRACE_HIT_WORLD; return u32(r, get(r, id, "model"), &out->model, e); }
    if (equal(r, kind, "actor")) {
        out->hit = QA_TRACE_HIT_ACTOR;
        return actor(r, get(r, id, "actor"), &out->actor, e) &&
            (out->actor.registry || fail(e, QA_ERROR_FORMAT, "Unified ground actor is absent"));
    }
    return fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown ground contact");
}
static bool kind(const prediction_reader *r, qa_json_id id, qa_movement_kind *out, qa_error *e)
{
    static const char *names[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (equal(r, id, names[i])) { *out = (qa_movement_kind)i; return true; }
    return fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown movement kernel");
}

#define V(field,name) vector(r,get(r,id,name),&(field),e)
#define F(field,name) real(r,get(r,id,name),&(field),e)
#define D(field,name) number(r,get(r,id,name),&(field),e)
#define I(field,name) i32(r,get(r,id,name),&(field),e)
#define U(field,name) u32(r,get(r,id,name),&(field),e)
#define B(field,name) boolean(r,get(r,id,name),&(field),e)
#define G(field,name) ground(r,get(r,id,name),&(field),e)
static bool state_read(const prediction_reader *r, qa_json_id id, qa_movement_state *out, qa_error *e)
{
    if (!kind(r, get(r,id,"kind"), &out->kind, e)) return false;
    switch (out->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        qa_nq_movement_state *v = &out->data.nq;
        return V(v->origin,"origin") && V(v->velocity,"velocity") && V(v->angles,"angles") &&
            V(v->old_origin,"oldOrigin") && V(v->angular_velocity,"angularVelocity") && V(v->view_angles,"viewAngles") &&
            V(v->punch_angles,"punchAngles") && V(v->water_jump_direction,"waterJumpDirection") &&
            I(v->move_type,"moveType") && U(v->flags,"flags") && G(v->ground,"ground") &&
            I(v->water_level,"waterLevel") && I(v->water_type,"waterType") && D(v->teleport_time_seconds,"teleportTimeSeconds") &&
            F(v->ideal_pitch,"idealPitch") && B(v->fix_angle,"fixAngle") && F(v->health,"health");
    }
    case QA_MOVEMENT_QUAKEWORLD: {
        qa_qw_movement_state *v = &out->data.qw;
        qa_json_id origin = get(r,id,"origin");
        return number(r,get(r,origin,"x"),&v->origin.x,e) && number(r,get(r,origin,"y"),&v->origin.y,e) &&
            number(r,get(r,origin,"z"),&v->origin.z,e) && V(v->velocity,"velocity") && V(v->angles,"angles") &&
            U(v->old_buttons,"oldButtons") && F(v->water_jump_time_seconds,"waterJumpTimeSeconds") &&
            B(v->dead,"dead") && I(v->spectator,"spectator") && G(v->ground,"ground");
    }
    case QA_MOVEMENT_Q2_CLASSIC: {
        qa_q2_movement_state *v = &out->data.q2;
        qa_json_id origin=get(r,id,"originEighths"), velocity=get(r,id,"velocityEighths"), delta=get(r,id,"deltaAngleShorts");
        qa_json_id storage=get(r,id,"coordinateStorage");
        if (storage!=QA_JSON_NONE) {
            if (!equal(r,storage,"q2pro-extended-v2")) return fail(e,QA_ERROR_FORMAT,"Unified Q2 state has an unknown coordinate storage");
            v->wide_coordinates=true;
        }
        if (qa_json_size(r->json,origin)!=3 || qa_json_size(r->json,velocity)!=3 || qa_json_size(r->json,delta)!=3)
            return fail(e,QA_ERROR_FORMAT,"Unified Q2 state changes its coordinate vector extent");
        for (size_t i=0;i<3;++i) {
            if (!i16(r,qa_json_at(r->json,delta,i),v->delta_angle_shorts+i,e)) return false;
            if (v->wide_coordinates) {
                if (!i32(r,qa_json_at(r->json,origin,i),v->wide.origin_eighths+i,e) ||
                    !i32(r,qa_json_at(r->json,velocity,i),v->wide.velocity_eighths+i,e)) return false;
            } else if (!i16(r,qa_json_at(r->json,origin,i),v->origin_eighths+i,e) ||
                !i16(r,qa_json_at(r->json,velocity,i),v->velocity_eighths+i,e)) return false;
        }
        return I(v->type,"type") && U(v->flags,"flags") &&
            (v->wide_coordinates ? u16(r,get(r,id,"timeMilliseconds"),&v->wide.time_ms,e) :
                u8(r,get(r,id,"timeEightMilliseconds"),&v->time_eight_ms,e)) &&
            i16(r,get(r,id,"gravity"),&v->gravity,e);
    }
    case QA_MOVEMENT_Q2_RERELEASE: {
        qa_q2r_movement_state *v=&out->data.q2r;
        return I(v->type,"type") && V(v->origin,"origin") && V(v->velocity,"velocity") && U(v->flags,"flags") &&
            U(v->time_ms,"timeMilliseconds") && i16(r,get(r,id,"gravity"),&v->gravity,e) &&
            V(v->delta_angles,"deltaAngles") && F(v->view_height,"viewHeight");
    }
    case QA_MOVEMENT_Q3: {
        qa_q3_movement_state *v=&out->data.q3;
        qa_json_id delta=get(r,id,"deltaAngleWords");
        if (qa_json_size(r->json,delta)!=3) return fail(e,QA_ERROR_FORMAT,"Unified Q3 state changes its angle vector extent");
        for (size_t i=0;i<3;++i) if (!i32(r,qa_json_at(r->json,delta,i),v->delta_angle_words+i,e)) return false;
        return I(v->command_time_ms,"commandTimeMilliseconds") && I(v->movement_type,"movementType") && I(v->bob_cycle,"bobCycle") &&
            U(v->movement_flags,"movementFlags") && I(v->movement_time_ms,"movementTimeMilliseconds") && V(v->origin,"origin") &&
            V(v->velocity,"velocity") && I(v->gravity,"gravity") && I(v->speed,"speed") && I(v->movement_direction,"movementDirection") &&
            V(v->grapple_point,"grapplePoint") && U(v->flags,"flags") && V(v->view_angles,"viewAngles") && F(v->view_height,"viewHeight") &&
            G(v->ground,"ground") && U(v->event_sequence,"predictableEventSequence") && actor(r,get(r,id,"jumpPad"),&v->jump_pad,e) &&
            I(v->movement_frame,"movementFrame") && I(v->jump_pad_frame,"jumpPadFrame");
    }
    }
    return false;
}
static bool parameters(const prediction_reader *r, qa_json_id id, qa_q1_movement_parameters *v, qa_error *e)
{
    return F(v->gravity,"gravity") && F(v->stop_speed,"stopSpeed") && F(v->max_speed,"maxSpeed") &&
        F(v->spectator_max_speed,"spectatorMaxSpeed") && F(v->accelerate,"accelerate") && F(v->air_accelerate,"airAccelerate") &&
        F(v->water_accelerate,"waterAccelerate") && F(v->friction,"friction") && F(v->water_friction,"waterFriction") &&
        F(v->entity_gravity,"entityGravity");
}
static bool numeric(const prediction_reader *r, qa_json_id id, qa_movement_kind k, int *rounding, qa_error *e)
{
#if !defined(__GNUC__) && !defined(__clang__)
    (void)r; (void)id; (void)k; (void)rounding;
    return fail(e,QA_ERROR_UNSUPPORTED,"Private native prediction has no declared C contraction policy");
#else
    qa_json_id arithmetic=get(r,id,"arithmetic");
    int32_t radix,scalar_bits,double_bits,evaluation,version;
    bool qw;
    if (!equal(r,get(r,id,"id"),"qa:numeric/movement-c") || !equal(r,get(r,id,"scalarStorage"),"binary32") ||
        !equal(r,get(r,id,"floatToInt"),"checked-c-truncation") || !equal(r,get(r,id,"integerOverflow"),"wrap32") ||
        !equal(r,get(r,arithmetic,"kind"),"native-c") || !equal(r,get(r,arithmetic,"kernel"),"qa-movement") ||
        !equal(r,get(r,arithmetic,"language"),"c17") || !equal(r,get(r,arithmetic,"contraction"),"off") ||
        !i32(r,get(r,arithmetic,"version"),&version,e) || version!=1 ||
        !i32(r,get(r,arithmetic,"radix"),&radix,e) || radix!=FLT_RADIX ||
        !i32(r,get(r,arithmetic,"scalarMantissaBits"),&scalar_bits,e) || scalar_bits!=FLT_MANT_DIG ||
        !i32(r,get(r,arithmetic,"doubleMantissaBits"),&double_bits,e) || double_bits!=DBL_MANT_DIG ||
        !i32(r,get(r,arithmetic,"evaluationMethod"),&evaluation,e) || evaluation!=FLT_EVAL_METHOD ||
        !boolean(r,get(r,arithmetic,"quakeWorldOriginBinary64"),&qw,e) || qw!=(k==QA_MOVEMENT_QUAKEWORLD))
        return fail(e,QA_ERROR_UNSUPPORTED,"Received movement arithmetic differs from the actual native kernel");
    qa_json_id value=get(r,arithmetic,"rounding");
    int expected=equal(r,value,"nearest-even")?FE_TONEAREST:equal(r,value,"toward-negative")?FE_DOWNWARD:
        equal(r,value,"toward-positive")?FE_UPWARD:equal(r,value,"toward-zero")?FE_TOWARDZERO:-1;
    if (expected<0 || expected!=fegetround()) return fail(e,QA_ERROR_UNSUPPORTED,"Private prediction has a different rounding environment");
    *rounding=expected; return true;
#endif
}
static bool clock_read(const prediction_reader *r, qa_json_id id, qa_movement_kind kind,
    const qa_clock_config *clock, qa_error *e)
{
    qa_clock_kind expected;
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE: expected=QA_CLOCK_NETQUAKE; break;
    case QA_MOVEMENT_QUAKEWORLD: expected=QA_CLOCK_QUAKEWORLD; break;
    case QA_MOVEMENT_Q2_CLASSIC: expected=QA_CLOCK_Q2_CLASSIC; break;
    case QA_MOVEMENT_Q2_RERELEASE: expected=QA_CLOCK_Q2_RERELEASE; break;
    case QA_MOVEMENT_Q3: expected=QA_CLOCK_Q3; break;
    default: return fail(e,QA_ERROR_FORMAT,"Prediction has no movement clock domain");
    }
    if (clock->kind!=expected)
        return fail(e,QA_ERROR_FORMAT,"Prediction clock differs from its admitted provider clock");
    double value;
    if (kind==QA_MOVEMENT_NETQUAKE) {
        qa_json_id fixed=get(r,id,"fixedFrameSeconds");
        if (!number(r,get(r,id,"minimumFrameSeconds"),&value,e) || value!=(double)clock->minimum_frame_ns/1e9 ||
            !number(r,get(r,id,"maximumFrameSeconds"),&value,e) || value!=(double)clock->maximum_frame_ns/1e9)
            return fail(e,QA_ERROR_FORMAT,"Prediction changes its admitted NetQuake frame bounds");
        return (!clock->interval_ns?qa_json_type(r->json,fixed)==QA_JSON_NULL:
            number(r,fixed,&value,e) && value==(double)clock->interval_ns/1e9) ||
            fail(e,QA_ERROR_FORMAT,"Prediction changes its admitted NetQuake fixed interval");
    }
    if (kind==QA_MOVEMENT_QUAKEWORLD) return true;
    return (number(r,get(r,id,kind==QA_MOVEMENT_Q3?"serverFrameMilliseconds":"frameMilliseconds"),&value,e) &&
        value==(double)clock->interval_ns/1e6) || fail(e,QA_ERROR_FORMAT,"Prediction changes its admitted server frame interval");
}
static bool profile_read(const prediction_reader *r, qa_json_id id, qa_movement_profile *v, int *rounding, qa_error *e)
{
    if (!kind(r,get(r,id,"kind"),&v->kind,e) || !numeric(r,get(r,id,"numeric"),v->kind,rounding,e)) return false;
    const qa_recipe_provider *provider=r->importing ? frontend_remote_unified_provider_published(r->replica,QA_ROLE_MOVEMENT,"") :
        frontend_remote_unified_provider(r->replica,QA_ROLE_MOVEMENT,"");
    if (!provider || !equal(r,get(r,id,"id"),provider->selection.instance))
        return fail(e,QA_ERROR_FORMAT,"Prediction profile differs from its actual received player movement provider");
    if (!clock_read(r,get(r,id,"clock"),v->kind,&provider->selection.clock,e)) return false;
    switch(v->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        qa_json_id edition=get(r,id,"edition");
        if (equal(r,edition,"classic")) v->data.nq.edition=QA_Q1_CLASSIC;
        else if (equal(r,edition,"rerelease")) v->data.nq.edition=QA_Q1_RERELEASE;
        else if (equal(r,edition,"quake64")) v->data.nq.edition=QA_Q1_QUAKE64;
        else return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown NetQuake edition");
        return parameters(r,get(r,id,"parameters"),&v->data.nq.parameters,e) && F(v->data.nq.edge_friction,"edgeFriction") &&
            F(v->data.nq.max_velocity,"maxVelocity") && F(v->data.nq.ideal_pitch_scale,"idealPitchScale") &&
            F(v->data.nq.roll_speed,"rollSpeed") && F(v->data.nq.roll_angle,"rollAngle") && B(v->data.nq.no_clip_angle_hack,"noClipAngleHack") &&
            B(v->data.nq.no_step,"noStep") && B(v->data.nq.source_jump_authority,"sourceJumpAuthority") &&
            B(v->data.nq.preserve_fixangle_roll,"preserveFixAngleRoll");
    }
    case QA_MOVEMENT_QUAKEWORLD: {
        qa_json_id clock=get(r,id,"clock");
        return parameters(r,get(r,id,"parameters"),&v->data.qw.parameters,e) &&
            u32(r,get(r,clock,"maximumCommandMilliseconds"),&v->data.qw.maximum_command_ms,e) && B(v->data.qw.shared_controls,"sharedControls");
    }
    case QA_MOVEMENT_Q2_CLASSIC:
        return F(v->data.q2.air_accelerate,"airAccelerate") && B(v->data.q2.snap_initial,"snapInitial") && B(v->data.q2.strafejump_hack,"strafejumpHack");
    case QA_MOVEMENT_Q2_RERELEASE:
        return F(v->data.q2r.air_accelerate,"airAccelerate") && B(v->data.q2r.n64_physics,"n64Physics");
    case QA_MOVEMENT_Q3: {
        qa_json_id product=get(r,id,"product"), fixed=get(r,id,"fixedMilliseconds");
        if (!equal(r,product,"baseq3") && !equal(r,product,"missionpack")) return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown Q3 product");
        v->data.q3.missionpack=equal(r,product,"missionpack");
        return (qa_json_type(r->json,fixed)==QA_JSON_NULL || u32(r,fixed,&v->data.q3.fixed_ms,e)) && B(v->data.q3.no_footsteps,"noFootsteps");
    }
    }
    return false;
}
static bool environment_read(const prediction_reader *r, qa_json_id id, qa_movement_environment *v, qa_error *e)
{
    if (!F(v->health,"health") || !B(v->flight,"flight") || !B(v->haste,"haste") || !B(v->invulnerable,"invulnerable") ||
        !F(v->gravity_multiplier,"gravityMultiplier") || !F(v->speed_multiplier,"speedMultiplier") ||
        !B(v->fixed_pose,"fixedPose") || !B(v->fixed_crouched,"fixedCrouched") ||
        !bounds(r,get(r,id,"poseBounds"),&v->pose.bounds,e) || !F(v->pose.view_height,"poseViewHeight")) return false;
    qa_json_id outputs=get(r,id,"clientOutputs"), mode=get(r,outputs,"mode"), stance=get(r,outputs,"stance"), body=get(r,outputs,"bodyBounds");
    if (mode!=QA_JSON_NONE) {
        v->has_mode=true;
        if (equal(r,mode,"normal")) v->mode=QA_MOVEMENT_MODE_NORMAL;
        else if (equal(r,mode,"noclip")) v->mode=QA_MOVEMENT_MODE_NOCLIP;
        else if (equal(r,mode,"freeze")) v->mode=QA_MOVEMENT_MODE_FREEZE;
        else return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown client movement mode");
    }
    if (stance!=QA_JSON_NONE) { v->has_stance=true; if(!boolean(r,stance,&v->crouched,e)) return false; }
    if (body!=QA_JSON_NONE) { v->has_body_bounds=true; if(!bounds(r,body,&v->body_bounds,e)) return false; }
    return (v->gravity_multiplier>=0 && v->speed_multiplier>=0) || fail(e,QA_ERROR_FORMAT,"Prediction has a negative environment multiplier");
}
#undef V
#undef F
#undef D
#undef I
#undef U
#undef B
#undef G

static bool scene_read(const prediction_reader *r, qa_json_id array, qa_world *scene, qa_error *e)
{
    for(size_t i=0;i<qa_json_size(r->json,array);++i) {
        qa_json_id row=qa_json_at(r->json,array,i), body=get(r,row,"body"), value=get(r,body,"state"), policy=get(r,row,"collision");
        qa_actor_id id; qa_saved_actor_id wire; qa_body_state state={0}; qa_actor_collision collision={0}; qa_body_link_state link={.linked=true}; int64_t count;
        if (!actor(r,get(r,body,"actor"),&id,e) || !id.registry ||
            !frontend_remote_unified_wire_actor(r->replica,id,&wire) ||
            !((r->importing ? frontend_remote_unified_actor_published(r->replica,wire.slot,wire.generation) :
                frontend_remote_unified_actor_present(r->replica,wire.slot,wire.generation)) ||
                fail(e,QA_ERROR_FORMAT,"Prediction collision body lacks its received live actor metadata")) ||
            !vector(r,get(r,value,"origin"),&state.origin,e) || !vector(r,get(r,value,"angles"),&state.angles,e) ||
            !vector(r,get(r,value,"velocity"),&state.velocity,e) || !bounds(r,get(r,value,"bounds"),&state.bounds,e) ||
            !actor(r,get(r,value,"ground"),&state.ground,e) || !bounds(r,get(r,body,"absoluteBounds"),&link.absolute_bounds,e) ||
            !integer(r,get(r,body,"linkCount"),1,QA_UNIFIED_SAFE_INTEGER,&count,e)) return false;
        qa_body_state duplicate;
        if (qa_world_body_read(scene,id,&duplicate,NULL)) return fail(e,QA_ERROR_FORMAT,"Prediction repeats a collision body actor");
        link.state=state; link.link_count=(uint64_t)count;
        qa_json_id family=get(r,policy,"family"), role=get(r,policy,"role"), shape=get(r,policy,"shape"), shape_kind=get(r,shape,"kind");
        if(equal(r,family,"q1")) collision.family=QA_COLLISION_Q1;
        else if(equal(r,family,"q2")) collision.family=QA_COLLISION_Q2;
        else if(equal(r,family,"q3")) collision.family=QA_COLLISION_Q3;
        else return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown collision family");
        if(equal(r,role,"solid")) collision.role=QA_COLLISION_SOLID;
        else if(equal(r,role,"trigger")) collision.role=QA_COLLISION_TRIGGER;
        else if(equal(r,role,"both")) collision.role=QA_COLLISION_BOTH;
        else return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown collision role");
        if(equal(r,shape_kind,"box")) collision.shape=QA_SHAPE_BOX;
        else if(equal(r,shape_kind,"capsule")) collision.shape=QA_SHAPE_CAPSULE;
        else if(equal(r,shape_kind,"model")) { collision.inline_model=true; if(!u32(r,get(r,shape,"model"),&collision.model,e)) return false; }
        else return fail(e,QA_ERROR_FORMAT,"Prediction has an unknown collision shape");
        if (!i32(r,get(r,policy,"contents"),&collision.contents,e) || !actor(r,get(r,policy,"owner"),&collision.owner,e) ||
            !boolean(r,get(r,policy,"monster"),&collision.monster,e) || !boolean(r,get(r,policy,"deadMonster"),&collision.dead_monster,e)) return false;
        qa_json_id corpse=get(r,policy,"q1Corpse"), owner=get(r,policy,"q3Owner");
        if(corpse!=QA_JSON_NONE && !boolean(r,corpse,&collision.q1_corpse,e)) return false;
        if(owner!=QA_JSON_NONE) {
            collision.has_q3_owner=true;
            if(!i32(r,get(r,owner,"entityNumber"),&collision.q3_entity_number,e) || !i32(r,get(r,owner,"ownerNumber"),&collision.q3_owner_number,e)) return false;
        }
        if(!qa_world_body_create(scene,id,&state,e) || !qa_world_set_collision(scene,id,&collision,e) ||
            !qa_world_restore_link_state(scene,id,&link,e)) return false;
    }
    return true;
}
static bool current(const frontend_remote_unified_prediction *p, qa_error *e)
{
    if(!p || !(p->importing ? frontend_remote_unified_checkpoint_current(p->replica,e) :
        frontend_remote_unified_current(p->replica,e)) || frontend_remote_unified_recipe(p->replica)!=p->recipe ||
        frontend_remote_unified_registry(p->replica)!=p->registry || frontend_remote_unified_epoch(p->replica)!=p->epoch ||
        qa_executable_recipe_geometry(p->recipe)!=p->geometry)
        return fail(e,QA_ERROR_ARGUMENT,"Private prediction changed its actual replica, recipe, registry or epoch");
    if(p->received && fegetround()!=p->rounding) return fail(e,QA_ERROR_UNSUPPORTED,"Private prediction changed its admitted rounding environment");
    return true;
}
static bool create(frontend_remote_unified *replica,bool importing,
    frontend_remote_unified_prediction **out, qa_error *e)
{
    if(!replica || !out || *out || !(importing ? frontend_remote_unified_checkpoint_current(replica,e) :
        frontend_remote_unified_current(replica,e))) return false;
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(replica);
    qa_actor_registry *registry=frontend_remote_unified_registry(replica);
    qa_collision_geometry *geometry=qa_executable_recipe_geometry(recipe);
    if(!recipe || !registry || !geometry) return fail(e,QA_ERROR_ARGUMENT,"Private prediction requires actual received map geometry and identities");
    frontend_remote_unified_prediction *p=calloc(1,sizeof(*p));
    if(!p) return fail(e,QA_ERROR_MEMORY,"Allocating private unified prediction owner");
    p->replica=replica; p->recipe=recipe; p->registry=registry; p->geometry=geometry;
    p->epoch=frontend_remote_unified_epoch(replica); p->discarded=-1;p->importing=importing;
    *out=p; return true;
}
bool frontend_remote_unified_prediction_create(frontend_remote_unified *replica,
    frontend_remote_unified_prediction **out,qa_error *e)
{ return create(replica,false,out,e); }
bool frontend_prediction_import_create(frontend_remote_unified *replica,
    frontend_remote_unified_prediction **out,qa_error *e)
{ return frontend_remote_unified_restore_pending(replica) && create(replica,true,out,e); }
bool frontend_remote_unified_prediction_receive(frontend_remote_unified_prediction *p,
    const qa_unified_document *document, qa_error *e)
{
    if(!p || p->busy || !document || qa_unified_document_type(document)!=QA_UNIFIED_PREDICTION_DOCUMENT || !current(p,e)) return false;
    p->busy=true;
    prediction_reader r={document,qa_unified_document_json(document),p->replica,p->importing};
    qa_json_id root=qa_unified_document_root(document);
    prediction_snapshot s={0}; qa_actor_id admitted; uint32_t source_entity; int rounding=0;
    qa_world *scene=NULL; qa_unified_document *copy=NULL;
    uint64_t authoritative_frame=0;
    const qa_unified_document *frame=p->importing?NULL:frontend_remote_unified_frame_prepared(p->replica);
    if(!frame) frame=frontend_remote_unified_frame(p->replica);
    const qa_json_document *frame_json=qa_unified_document_json(frame);
    qa_json_id frame_snapshot=qa_json_get(frame_json,qa_json_get(frame_json,qa_unified_document_root(frame),"output"),"snapshot");
    bool ok=frontend_remote_unified_player(p->replica,&admitted,&source_entity) && actor(&r,get(&r,root,"actor"),&s.input.actor,e) &&
        frame&&qa_json_u64(frame_json,qa_json_get(frame_json,qa_json_get(frame_json,frame_snapshot,"frame"),"frame"),&authoritative_frame,e)&&
        qa_actor_id_equal(admitted,s.input.actor) && integer(&r,get(&r,root,"sequence"),-1,QA_UNIFIED_SAFE_INTEGER,&s.sequence,e) &&
        number(&r,get(&r,root,"commandTimeMilliseconds"),&s.time_ms,e) && state_read(&r,get(&r,root,"state"),&s.input.state,e) &&
        profile_read(&r,get(&r,root,"profile"),&s.input.profile,&rounding,e) && s.input.profile.kind==s.input.state.kind &&
        environment_read(&r,get(&r,root,"environment"),&s.input.environment,e) && bounds(&r,get(&r,root,"standingBounds"),&s.input.standing.bounds,e) &&
        real(&r,get(&r,root,"standingViewHeight"),&s.input.standing.view_height,e) && bounds(&r,get(&r,root,"bounds"),&s.input.current_bounds,e) &&
        vector(&r,get(&r,root,"viewAngles"),&s.angles,e) && vector(&r,get(&r,root,"viewOffset"),&s.offset,e) && real(&r,get(&r,root,"viewHeight"),&s.height,e);
    qa_json_id postures=get(&r,root,"nativePostures"), contact=get(&r,root,"contact");
    if(ok) ok=bounds(&r,get(&r,postures,"crouchedBounds"),&s.input.crouched.bounds,e) && real(&r,get(&r,postures,"crouchedViewHeight"),&s.input.crouched.view_height,e) &&
        bounds(&r,get(&r,postures,"deadBounds"),&s.input.dead.bounds,e) && real(&r,get(&r,postures,"deadViewHeight"),&s.input.dead.view_height,e) &&
        bounds(&r,get(&r,postures,"invulnerabilityBounds"),&s.input.invulnerability_bounds,e) && ground(&r,get(&r,contact,"ground"),&s.ground,e) &&
        i32(&r,get(&r,contact,"waterLevel"),&s.water_level,e) && i32(&r,get(&r,contact,"waterType"),&s.water_type,e);
    if(ok && s.input.state.kind==QA_MOVEMENT_Q2_RERELEASE) ok=vector(&r,get(&r,root,"rereleaseOrigin"),&s.pml,e);
    if(ok) ok=qa_world_create(p->registry,p->geometry,NULL,&scene,e) && scene_read(&r,get(&r,root,"collisions"),scene,e) &&
        qa_unified_document_retain(document,&copy,e) && current(p,e);
    if(ok && p->received && (s.sequence<p->snapshot.sequence || s.input.state.kind!=p->snapshot.input.state.kind))
        ok=fail(e,QA_ERROR_FORMAT,"Prediction snapshot rewinds its acknowledgement or changes movement family");
    if(ok && p->scene) ok=qa_world_destroy(p->scene,e);
    if(ok) {
        qa_unified_document_destroy(p->snapshot_document);
        s.input.command.kind=s.input.state.kind; s.input.prediction=true; s.input.has_current_bounds=true;
        s.input.shape=(qa_trace_shape){QA_SHAPE_BOX,s.input.standing.bounds}; s.input.q1_solid=QA_Q1_SOLID_SLIDEBOX;
        s.input.view_offset=s.offset;
        p->scene=scene; scene=NULL; p->snapshot_document=copy; copy=NULL; p->snapshot=s; p->received=true; p->rounding=rounding;
        p->authoritative_frame=authoritative_frame;
        size_t retired=0;
        while(retired<p->command_count && (int64_t)p->commands[retired].sequence<=s.sequence) ++retired;
        memmove(p->commands,p->commands+retired,(p->command_count-retired)*sizeof(*p->commands)); p->command_count-=retired;
    }
    qa_unified_document_destroy(copy);
    if(scene) (void)qa_world_destroy(scene,NULL);
    p->busy=false;
    if (!ok && (!e || e->code==QA_OK))
        fail(e,QA_ERROR_FORMAT,"Unified prediction does not match its actual admitted player snapshot");
    return ok;
}
static bool same_number(double a, double b)
{ return a==b && (a!=0 || signbit(a)==signbit(b)); }
static bool same_command(const qa_unified_movement *a, const qa_unified_movement *b)
{
    if(a->kind!=b->kind) return false;
#define EQ(field) same_number(a->data.field,b->data.field)
    switch(a->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return EQ(nq.acknowledged_seconds) && EQ(nq.angles.x) && EQ(nq.angles.y) && EQ(nq.angles.z) &&
            EQ(nq.forward) && EQ(nq.side) && EQ(nq.up) && EQ(nq.buttons) && EQ(nq.impulse);
    case QA_MOVEMENT_QUAKEWORLD:
        return EQ(qw.milliseconds) && EQ(qw.angles.x) && EQ(qw.angles.y) && EQ(qw.angles.z) &&
            EQ(qw.forward) && EQ(qw.side) && EQ(qw.up) && EQ(qw.buttons) && EQ(qw.impulse);
    case QA_MOVEMENT_Q2_CLASSIC:
        return EQ(q2.milliseconds) && EQ(q2.angle_shorts[0]) && EQ(q2.angle_shorts[1]) && EQ(q2.angle_shorts[2]) &&
            EQ(q2.forward) && EQ(q2.side) && EQ(q2.up) && EQ(q2.buttons) && EQ(q2.impulse) && EQ(q2.light_level);
    case QA_MOVEMENT_Q2_RERELEASE:
        return EQ(q2r.milliseconds) && EQ(q2r.angles.x) && EQ(q2r.angles.y) && EQ(q2r.angles.z) &&
            EQ(q2r.forward) && EQ(q2r.side) && EQ(q2r.buttons) && EQ(q2r.server_frame);
    case QA_MOVEMENT_Q3:
        return EQ(q3.server_time_ms) && EQ(q3.angle_words[0]) && EQ(q3.angle_words[1]) && EQ(q3.angle_words[2]) &&
            EQ(q3.buttons) && EQ(q3.weapon) && EQ(q3.forward) && EQ(q3.right) && EQ(q3.up);
    }
#undef EQ
    return false;
}
bool frontend_remote_unified_prediction_input(frontend_remote_unified_prediction *p,
    const qa_unified_input *input, double time_ms, qa_error *e)
{
    if(!p || p->busy || !p->received || !input || !isfinite(time_ms) || input->sequence>QA_UNIFIED_SAFE_INTEGER ||
        input->command.kind!=p->snapshot.input.state.kind || !current(p,e)) return false;
    qa_movement_command probe;
    if(!qa_application_control_project_unified(&input->command,&p->snapshot.input.state,input->sequence,&probe,e)) return false;
    for(size_t i=0;i<p->command_count;++i) if(input->sequence==p->commands[i].sequence)
        return (same_number(time_ms,p->commands[i].time_ms) && same_command(&input->command,&p->commands[i].raw)) ||
            fail(e,QA_ERROR_ARGUMENT,"Prediction retry changes its retained command or source time");
    if((int64_t)input->sequence<=p->snapshot.sequence || (p->command_count && input->sequence<=p->commands[p->command_count-1].sequence)) return true;
    if(p->command_count==64) {
        p->discarded=(int64_t)p->commands[0].sequence;
        memmove(p->commands,p->commands+1,63*sizeof(*p->commands)); --p->command_count;
    }
    p->commands[p->command_count++]=(prediction_command){input->command,input->sequence,time_ms};
    return true;
}
static bool trace(void *context, const qa_trace_query *q, qa_trace_result *out, qa_error *e)
{ return qa_world_trace(((frontend_remote_unified_prediction *)context)->scene,q,out,e); }
static bool contents(void *context, const qa_point_query *q, qa_point_contents *out, qa_error *e)
{ return qa_world_point_contents(((frontend_remote_unified_prediction *)context)->scene,q,out,e); }
static bool firing(void *context, const qa_movement_call *call)
{ (void)context; return (call->command->buttons&1u)!=0 && call->environment->health>0; }
static bool brush(void *context, const qa_trace_result *hit, bool *out, qa_error *e)
{
    frontend_remote_unified_prediction *p=context;
    if(hit->hit==QA_TRACE_HIT_WORLD) { *out=true; return true; }
    if(hit->hit==QA_TRACE_HIT_NONE) { *out=false; return true; }
    qa_actor_collision c;
    if(!qa_world_get_collision(p->scene,hit->actor,&c,e)) return false;
    *out=c.inline_model; return true;
}
static bool replay(frontend_remote_unified_prediction *p, prediction_snapshot *s,
    frontend_unified_prediction_status *status,const int32_t *prior_command_time,bool *matched,qa_error *e)
{
    if(matched) *matched=false;
    *s=p->snapshot;
    *status=FRONTEND_UNIFIED_PREDICTION_UNCHANGED;
    bool disabled=(s->input.state.kind==QA_MOVEMENT_Q2_CLASSIC && (s->input.state.data.q2.flags&64u)) ||
        (s->input.state.kind==QA_MOVEMENT_Q2_RERELEASE && (s->input.state.data.q2r.flags&64u));
    if(disabled) {
        *status=FRONTEND_UNIFIED_PREDICTION_DISABLED;
        if(p->command_count) {
            qa_movement_command command;
            const prediction_command *last=p->commands+p->command_count-1;
            if(!qa_application_control_project_unified(&last->raw,&s->input.state,last->sequence,&command,e)) return false;
            if(command.kind==QA_MOVEMENT_Q2_CLASSIC) {
                float angles[3];
                for(unsigned i=0;i<3;++i) {
                    uint16_t word=(uint16_t)command.angle_words[i];
                    int32_t angle=word<=INT16_MAX?word:(int32_t)word-65536;
                    angles[i]=(float)(angle*(360.0/65536.0)+s->input.state.data.q2.delta_angle_shorts[i]*(360.0/65536.0));
                }
                s->angles=qa_v3(angles[0],angles[1],angles[2]);
            } else s->angles=qa_vec_add(command.angles,s->input.state.data.q2r.delta_angles);
        }
        return true;
    }
    uint64_t last=p->command_count?p->commands[p->command_count-1].sequence:0;
    if(p->discarded>s->sequence || (p->command_count && (double)last-(double)s->sequence>=63)) { *status=FRONTEND_UNIFIED_PREDICTION_EXHAUSTED; return true; }
    qa_movement_services services={.context=p,.trace=trace,.point_contents=contents,.firing=firing,.is_bsp=brush};
    qa_movement_result result={0};
    bool ok=true;
    for(size_t i=0;ok && i<p->command_count;++i) {
        const prediction_command *entry=p->commands+i;
        qa_movement_input in=s->input;
        in.view_offset=s->offset; in.q2r_pml_origin=&s->pml; in.snap_initial=i==0;
        ok=qa_application_control_project_unified(&entry->raw,&in.state,entry->sequence,&in.command,e);
        if(!ok) break;
        double duration=in.state.kind==QA_MOVEMENT_NETQUAKE || in.state.kind==QA_MOVEMENT_Q3?fmax(0,entry->time_ms-s->time_ms):in.command.milliseconds;
        if(!isfinite(duration) || duration>=(double)UINT64_MAX/1000000.0 || entry->time_ms>=(double)UINT64_MAX/1000000.0)
            { ok=fail(e,QA_ERROR_FORMAT,"Prediction clock exceeds its native duration domain"); break; }
        in.elapsed_ns=(uint64_t)(duration*1000000.0);
        in.time_ns=entry->time_ms<=0?0:(uint64_t)(entry->time_ms*1000000.0);
        in.has_source_seconds=in.state.kind==QA_MOVEMENT_NETQUAKE; in.source_seconds=entry->time_ms/1000;
        if(in.state.kind==QA_MOVEMENT_Q2_CLASSIC) in.profile.data.q2.snap_initial=false;
        if(in.state.kind==QA_MOVEMENT_Q3) in.environment.gravity_multiplier=1;
        if(in.state.kind==QA_MOVEMENT_QUAKEWORLD && in.environment.has_stance) { in.profile.data.qw.shared_controls=true; in.shape.bounds=in.current_bounds; }
        bool boundary=prior_command_time&&in.state.kind==QA_MOVEMENT_Q3&&
            in.state.data.q3.command_time_ms==*prior_command_time&&
            in.command.server_time_ms>in.state.data.q3.command_time_ms;
        ok=qa_movement_move(&in,&services,&result,e);
        if(ok && result.status!=QA_MOVEMENT_ACTIVE) ok=fail(e,QA_ERROR_FORMAT,"Private movement cannot retire an authoritative actor");
        if(ok) {
            if(boundary&&result.state.kind==QA_MOVEMENT_Q3&&
                result.state.data.q3.command_time_ms!=in.state.data.q3.command_time_ms&&matched) *matched=true;
            s->input.state=result.state; s->input.current_bounds=result.bounds;
            s->angles=result.view_angles; s->height=result.view_height;
            s->ground=result.ground; s->water_level=result.water_level; s->water_type=result.water_type;
            s->sequence=(int64_t)entry->sequence; s->time_ms=entry->time_ms;
            *status=FRONTEND_UNIFIED_PREDICTION_ACTIVE;
        }
    }
    qa_movement_result_free(&result);
    return ok;
}
static bool read_prediction(frontend_remote_unified_prediction *p,frontend_unified_prediction_view *out,
    const int32_t *prior_command_time,bool *matched,qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    p->busy=true;
    prediction_snapshot s; frontend_unified_prediction_status status;
    bool ok=replay(p,&s,&status,prior_command_time,matched,e) && current(p,e);
    if(ok) *out=(frontend_unified_prediction_view){.actor=s.input.actor,.state=s.input.state,
        .origin_shift=qa_vec_sub(qa_movement_origin(&s.input.state),qa_movement_origin(&p->snapshot.input.state)),
        .view_angles=s.angles,.view_offset=s.offset,.bounds=s.input.current_bounds,.ground=s.ground,.view_height=s.height,
        .command_time_ms=s.time_ms,.sequence=s.sequence,.authoritative_frame=p->authoritative_frame,.status=status};
    p->busy=false; return ok;
}
bool frontend_remote_unified_prediction_read(frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out,qa_error *e)
{ return read_prediction(p,out,NULL,NULL,e); }
bool frontend_remote_unified_prediction_read_command_boundary(frontend_remote_unified_prediction *p,
    int32_t prior_command_time,frontend_unified_prediction_view *out,bool *matched,qa_error *e)
{
    if(!matched) return fail(e,QA_ERROR_ARGUMENT,"Q3 replay boundary requires its output receipt");
    bool actual=false;
    if(!read_prediction(p,out,&prior_command_time,&actual,e)) return false;
    *matched=actual; return true;
}
bool frontend_remote_unified_prediction_time(const frontend_remote_unified_prediction *p, double *out, qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    *out=p->command_count?fmax(p->snapshot.time_ms,p->commands[p->command_count-1].time_ms):p->snapshot.time_ms;
    return true;
}
static void snapshot_read(const frontend_remote_unified_prediction *p,frontend_unified_prediction_view *out)
{
    const prediction_snapshot *s=&p->snapshot;
    *out=(frontend_unified_prediction_view){.actor=s->input.actor,.state=s->input.state,
        .view_angles=s->angles,.view_offset=s->offset,.bounds=s->input.current_bounds,.ground=s->ground,
        .view_height=s->height,.command_time_ms=s->time_ms,.sequence=s->sequence,.authoritative_frame=p->authoritative_frame,
        .status=FRONTEND_UNIFIED_PREDICTION_UNCHANGED};
}
bool frontend_remote_unified_prediction_snapshot(const frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out, qa_error *e)
{
    if(!p || p->busy || !p->received || !out || !current(p,e)) return false;
    snapshot_read(p,out);return true;
}
bool frontend_prediction_checkpoint_snapshot(const frontend_remote_unified_prediction *p,
    frontend_unified_prediction_view *out,qa_error *e)
{
    if(!p || p->busy || p->importing || !p->received || !out ||
        !frontend_remote_unified_checkpoint_current(p->replica,e) ||
        p->recipe!=frontend_remote_unified_recipe(p->replica) ||
        p->registry!=frontend_remote_unified_registry(p->replica) ||
        p->epoch!=frontend_remote_unified_epoch(p->replica) ||
        p->geometry!=qa_executable_recipe_geometry(p->recipe)) return false;
    snapshot_read(p,out);return true;
}
bool frontend_remote_unified_prediction_idle(const frontend_remote_unified_prediction *p)
{ return p && !p->busy && (!p->scene || qa_world_idle(p->scene)); }
static bool received_actor(const frontend_remote_unified_prediction *p,qa_actor_id actor_id)
{
    qa_saved_actor_id wire;
    return qa_actors_get(p->registry,actor_id) && frontend_remote_unified_wire_actor(p->replica,actor_id,&wire) &&
        frontend_remote_unified_actor_published(p->replica,wire.slot,wire.generation);
}
bool frontend_remote_unified_prediction_trace(frontend_remote_unified_prediction *p,
    const qa_trace_query *query,qa_trace_result *out,qa_error *e)
{
    if (!p || !query || !out || !p->received || !p->scene ||
        !frontend_remote_unified_prediction_idle(p) || !current(p,e) ||
        (query->pass_actor.registry && !received_actor(p,query->pass_actor)))
        return fail(e,QA_ERROR_ARGUMENT,"Unified trace requires its returned received world and actual actor namespace");
    qa_trace_result result;
    p->busy=true;
    bool ok=qa_world_trace(p->scene,query,&result,e);
    p->busy=false;
    if (ok) ok=current(p,e) && (result.hit!=QA_TRACE_HIT_ACTOR || received_actor(p,result.actor));
    if (ok) *out=result;
    return ok;
}
bool frontend_remote_unified_prediction_body_read(const frontend_remote_unified_prediction *p,
    qa_actor_id actor_id,qa_body_state *out,qa_error *e)
{
    qa_body_state body;
    if (!p || !out || !p->received || !p->scene || !frontend_remote_unified_prediction_idle(p) ||
        !current(p,e) || !received_actor(p,actor_id))
        return fail(e,QA_ERROR_ARGUMENT,"Unified body read requires its actual received collision body");
    if (!qa_world_body_read(p->scene,actor_id,&body,e) || !current(p,e)) return false;
    *out=body; return true;
}
bool frontend_remote_unified_prediction_player_origin(frontend_remote_unified_prediction *p,qa_vec3 *out,qa_error *e)
{
    frontend_unified_prediction_view view; qa_body_state body;
    if(!out||!frontend_remote_unified_prediction_read(p,&view,e)||
        !frontend_remote_unified_prediction_body_read(p,view.actor,&body,e)||!current(p,e)) return false;
    *out=qa_vec_add(body.origin,view.origin_shift); return true;
}
bool frontend_remote_unified_prediction_point_contents(frontend_remote_unified_prediction *p,
    const qa_point_query *query,qa_point_contents *out,qa_error *e)
{
    if(!p||!query||!out||!p->received||!p->scene||!frontend_remote_unified_prediction_idle(p)||
        !current(p,e)||(query->pass_actor.registry&&!received_actor(p,query->pass_actor)))
        return fail(e,QA_ERROR_ARGUMENT,"Unified contents requires its returned received world and actual actor namespace");
    qa_point_contents result;
    p->busy=true;
    bool ok=qa_world_point_contents(p->scene,query,&result,e);
    p->busy=false;
    if(ok) ok=current(p,e);
    if(ok) *out=result;
    return ok;
}
const qa_unified_document *frontend_remote_unified_prediction_document(
    const frontend_remote_unified_prediction *p)
{
    qa_error e={0};
    return p && !p->busy && p->received && current(p,&e)?p->snapshot_document:NULL;
}
static bool q3_ground_number(const frontend_unified_q3_prediction_source *source,
    qa_movement_ground ground,int32_t *out,qa_error *e)
{
    if(ground.hit==QA_TRACE_HIT_WORLD) {*out=1022;return true;}
    if(ground.hit==QA_TRACE_HIT_NONE) {*out=1023;return true;}
    if(ground.hit!=QA_TRACE_HIT_ACTOR) return fail(e,QA_ERROR_FORMAT,"Q3 prediction ground has no actual collision role");
    uint32_t number=0;bool found=false;
    if(!source->number(source->context,ground.actor,&number,&found,e)) return false;
    if(found&&number>=1022) return fail(e,QA_ERROR_FORMAT,"Q3 actor ground maps outside actual Source entities");
    *out=found?(int32_t)number:1023;return true;
}
bool frontend_remote_unified_prediction_merged_q3(frontend_remote_unified_prediction *p,
    const qa_q3_player *baseline,qa_actor_id viewer,const frontend_unified_q3_prediction_source *source,
    qa_q3_player *out,frontend_unified_prediction_view *view,qa_error *e)
{
    if(!baseline||!out||!view||!source||!source->current||!source->number||
        !source->current(source->context,e)) return fail(e,QA_ERROR_ARGUMENT,"Q3 prediction merge requires its actual retained Source receipt");
    uint32_t viewer_number=0;bool found=false;
    frontend_unified_prediction_view predicted;
    if(!source->number(source->context,viewer,&viewer_number,&found,e)||!found||viewer_number>=1022||
        baseline->clientNum!=(int32_t)viewer_number||!frontend_remote_unified_prediction_read(p,&predicted,e)||
        !qa_actor_id_equal(viewer,predicted.actor)) return fail(e,QA_ERROR_ARGUMENT,"Q3 prediction merge changed its full Source viewer");
    qa_q3_player merged=*baseline;
    qa_vec3 origin=qa_movement_origin(&predicted.state),velocity=qa_movement_velocity(&predicted.state);
    merged.origin[0]=origin.x;merged.origin[1]=origin.y;merged.origin[2]=origin.z;
    merged.velocity[0]=velocity.x;merged.velocity[1]=velocity.y;merged.velocity[2]=velocity.z;
    merged.viewangles[0]=predicted.view_angles.x;merged.viewangles[1]=predicted.view_angles.y;merged.viewangles[2]=predicted.view_angles.z;
    merged.viewheight=qa_source_float_to_i32(predicted.view_height);
    if(!isfinite(predicted.command_time_ms)||predicted.command_time_ms < -0x1p63||predicted.command_time_ms >= 0x1p63)
        return fail(e,QA_ERROR_FORMAT,"Q3 prediction clock exceeds native millisecond storage");
    uint32_t clock=(uint32_t)(int64_t)predicted.command_time_ms;
    memcpy(&merged.commandTime,&clock,sizeof(clock));
    if(!q3_ground_number(source,predicted.ground,&merged.groundEntityNum,e)) return false;
    if(predicted.state.kind==QA_MOVEMENT_Q3) {
        const qa_q3_movement_state *state=&predicted.state.data.q3;
        merged.commandTime=state->command_time_ms;merged.pmType=state->movement_type;
        memcpy(&merged.pmFlags,&state->movement_flags,sizeof(merged.pmFlags));
        merged.pmTime=state->movement_time_ms;merged.bobCycle=state->bob_cycle;
        for(unsigned i=0;i<3;++i) merged.deltaAngles[i]=state->delta_angle_words[i];
        if(!q3_ground_number(source,state->ground,&merged.groundEntityNum,e)) return false;
        merged.movementDir=state->movement_direction;
        memcpy(&merged.eFlags,&state->flags,sizeof(merged.eFlags));
        merged.pmoveFramecount=state->movement_frame;merged.jumppadFrame=state->jump_pad_frame;merged.jumppadEnt=0;
        if(state->jump_pad.registry) {
            uint32_t number=0;bool present=false;
            if(!source->number(source->context,state->jump_pad,&number,&present,e)) return false;
            if(present&&number>=1022) return fail(e,QA_ERROR_FORMAT,"Q3 jump pad maps outside actual Source entities");
            if(present) merged.jumppadEnt=(int32_t)number;
        }
    }
    if(!current(p,e)||!source->current(source->context,e)) return false;
    *out=merged;*view=predicted;return true;
}
bool frontend_remote_unified_prediction_destroy(frontend_remote_unified_prediction **owned, qa_error *e)
{
    if(!owned || !*owned) return true;
    frontend_remote_unified_prediction *p=*owned;
    if(!frontend_remote_unified_prediction_idle(p)) return fail(e,QA_ERROR_ARGUMENT,"Private prediction is entered during teardown");
    if(p->scene && !qa_world_destroy(p->scene,e)) return false;
    qa_unified_document_destroy(p->snapshot_document);
    free(p); *owned=NULL; return true;
}
