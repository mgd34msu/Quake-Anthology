#include "view_settings.h"
#include "save_private.h"
#include "qa/console_cvar_observer.h"
#include "qa/cvars_alias.h"
#include "qa/text.h"
#include "config_store.h"
#include "qa/network_q1_nq.h"
#include "qa/application_network.h"
#include "qa/application_network_qw.h"
#include <math.h>

struct frontend_view_settings {
    qa_frontend *frontend;
    qa_application *application;
    qa_cvars *registry;
    qa_cvar_handle fov, q1_view[6];
    frontend_view_settings **installed;
    frontend_view_preparation *preparation;
    void *context;
    bool (*changed)(void *,double,qa_error *);
    qa_cvar_observer_token observer;
    double value;
    uint64_t modification_count;
    bool explicit_override,published,notifying;
};
struct frontend_view_preparation {
    frontend_view_settings *parent;
    const qa_launch_snapshot *candidate;
    const qa_application_client_preparation *client;
    const qa_cvars_edit *edit;
    char *value;
    double number,previous_number;
    uint64_t modification_count;
    frontend_view_transition transition;
    bool previous_explicit,previous_published,explicit_override,notify,published,applied;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static bool decimal(const char *value,double *out,qa_error *e)
{
    const unsigned char *p=(const unsigned char *)value;
    if (!p) return fail(e,"Field of view requires a decimal value between 60 and 160");
    if (*p=='+' || *p=='-') ++p;
    size_t before=0,after=0;
    while (*p>='0' && *p<='9') { ++before; ++p; }
    if (*p=='.') {
        ++p;
        while (*p>='0' && *p<='9') { ++after; ++p; }
    }
    double number=0;
    if (*p || (!before && !after) || !qa_parse_number(
        (qa_bytes){(const uint8_t *)value,strlen(value)},&number,e) ||
        !isfinite(number) || number<60 || number>160)
        return fail(e,"Field of view requires a decimal value between 60 and 160");
    if (out) *out=number;
    return true;
}
static bool validate(void *context,const char *value,qa_error *e)
{ (void)context; return decimal(value,NULL,e); }
static void view_cvars_bind(frontend_view_settings *owner)
{
    owner->fov=qa_cvars_resolve(owner->registry,qa_cvars_canonical_name(owner->registry,"fov"));
    const char *const names[]={"viewsize","cl_sbar","chase_active","chase_back","chase_up","chase_right"};
    for (size_t i=0;i<6;++i)
        owner->q1_view[i]=qa_cvars_resolve(owner->registry,qa_cvars_canonical_name(owner->registry,names[i]));
}
static const qa_cvar_view *record(const frontend_view_settings *owner)
{
    return owner?qa_cvars_read(owner->registry,owner->fov):NULL;
}
bool frontend_view_settings_parent_is(const frontend_view_settings *owner,
    const qa_frontend *f,const qa_cvars *registry)
{
    return owner && f && owner->frontend==f && owner->application==f->application &&
        owner->registry==registry && registry==qa_application_cvars(owner->application) &&
        owner->installed && *owner->installed==owner && owner->observer && record(owner);
}
static bool current(const frontend_view_settings *owner)
{ return owner && frontend_view_settings_parent_is(owner,owner->frontend,owner->registry); }
static bool value_current(const frontend_view_settings *owner)
{
    const qa_cvar_view *row=current(owner)?record(owner):NULL;
    return row && row->modification_count==owner->modification_count &&
        row->number==(float)owner->value && row->integer==(int32_t)owner->value;
}
static bool observed(void *context,qa_cvars *registry,const char *name,qa_error *e)
{
    frontend_view_settings *owner=context;
    const qa_cvar_view *row=current(owner) && registry==owner->registry?record(owner):NULL;
    if (!row || strcmp(row->name,name) || owner->notifying)
        return fail(e,"View publication lost its actual canonical preference owner");
    if (owner->preparation) {
        const frontend_view_preparation *held=owner->preparation;
        return (held->published && !strcmp(row->value,held->value) &&
            row->modification_count==held->modification_count) ||
            fail(e,"Candidate view observation has no matching published child");
    }
    double value;
    if (!decimal(row->value,&value,e)) return false;
    owner->value=value; owner->modification_count=row->modification_count;
    owner->explicit_override=true;
    owner->notifying=true;
    bool ok=owner->changed(owner->context,value,e);
    owner->notifying=false;
    return ok;
}
bool frontend_view_settings_create(qa_frontend *f,qa_cvars *registry,void *context,
    bool (*changed)(void *,double,qa_error *),frontend_view_settings **out,qa_error *e)
{
    if (!f || !f->application || registry!=qa_application_cvars(f->application) ||
        !out || *out || !changed || !qa_cvars_observer_idle(registry))
        return fail(e,"View settings require the returned canonical registry and real recipient");
    const char *name=qa_cvars_canonical_name(registry,"fov");
    const qa_cvar_view *row=qa_cvars_find(registry,name);
    if (!row) return fail(e,"View settings require their actual registered fov");
    double number;
    if (!decimal(row->value,&number,NULL)) {
        if (!qa_cvars_set(registry,name,"90",true,e)) return false;
        row=qa_cvars_find(registry,name); number=90;
    }
    frontend_view_settings *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining explicit view preference");
    owner->frontend=f; owner->application=f->application; owner->registry=registry;
    view_cvars_bind(owner);
    owner->installed=out; owner->context=context; owner->changed=changed;
    owner->value=number; owner->modification_count=row->modification_count;
    owner->explicit_override=number!=90;
    if (!qa_cvars_bind(registry,name,&(qa_cvar_binding){.owner=QA_FRONTEND_COMMAND_OWNER,
        .user=owner,.validate=validate},e)) { free(owner); return false; }
    if (!qa_cvars_observe(registry,name,QA_FRONTEND_COMMAND_OWNER,observed,owner,&owner->observer,e)) {
        qa_cvars_unbind(registry,name,QA_FRONTEND_COMMAND_OWNER); free(owner); return false;
    }
    *out=owner; return true;
}
bool frontend_view_settings_read(const frontend_view_settings *owner,double *value,bool *explicit_override)
{
    if (!value || !explicit_override || !value_current(owner)) return false;
    *value=owner->value; *explicit_override=owner->explicit_override; return true;
}
bool frontend_view_settings_has_published(const frontend_view_settings *owner,bool *out)
{
    if (!out || !current(owner) || owner->preparation || owner->notifying) return false;
    *out=owner->published; return true;
}
bool frontend_view_settings_preferences(const frontend_view_settings *owner,frontend_shared_view_preferences *out)
{
    double value; bool explicit_override;
    if (!out || !frontend_view_settings_read(owner,&value,&explicit_override)) return false;
    *out=(frontend_shared_view_preferences){.field_of_view=explicit_override?value:0,.present=explicit_override};
    return true;
}
static char *copy(const char *value,qa_error *e)
{
    char *out=malloc(strlen(value)+1);
    if (!out) { frontend_fail(e,QA_ERROR_MEMORY,"Retaining candidate view scalar"); return NULL; }
    strcpy(out,value); return out;
}
bool frontend_view_settings_q1_sample(frontend_view_settings *owner,qa_ruleset_id dialect,
    frontend_q1_view_settings *out,qa_error *e)
{
    if (!current(owner) || !out || (dialect!=QA_RULESET_NETQUAKE && dialect!=QA_RULESET_QUAKEWORLD) ||
        owner->preparation || owner->notifying || !qa_cvars_observer_idle(owner->registry))
        return fail(e,"Q1 view settings require their returned published canonical parent");
    const char *size_name=NULL;
    double numbers[6];
    for (size_t i=0;i<6;++i) {
        const qa_cvar_view *row=qa_cvars_read(owner->registry,owner->q1_view[i]);
        if (!row || !isfinite(row->number))
            return fail(e,"Q1 view settings lost an actual finite canonical record");
        if (!i) size_name=row->name;
        numbers[i]=row->number;
    }
    if (numbers[0]<30 || numbers[0]>120) {
        const char *value=numbers[0]<30?"30":"120";
        if (!qa_cvars_set(owner->registry,size_name,value,false,e)) return false;
        numbers[0]=numbers[0]<30?30:120;
    }
    *out=(frontend_q1_view_settings){.size=numbers[0],
        .overlay_status=dialect==QA_RULESET_QUAKEWORLD && numbers[1]==0,
        .chase=dialect==QA_RULESET_NETQUAKE && numbers[2]!=0,
        .back=numbers[3],.up=numbers[4],.right=numbers[5]};
    return true;
}
static const struct { const char *name, *initial; bool offset; } motion_declarations[] = {
    {"cl_bob", "0.02", false}, {"cl_bobcycle", "0.6", false}, {"cl_bobup", "0.5", false},
    {"cl_rollspeed", "200", false}, {"cl_rollangle", "2.0", false},
    {"gl_cshiftpercent", "100", false},
    {"v_kicktime", "0.5", false}, {"v_kickroll", "0.6", false}, {"v_kickpitch", "0.6", false},
    {"v_idlescale", "0", false}, {"v_ipitch_cycle", "1", false},
    {"v_iyaw_cycle", "2", false}, {"v_iroll_cycle", "0.5", false},
    {"v_ipitch_level", "0.3", false}, {"v_iyaw_level", "0.3", false}, {"v_iroll_level", "0.1", false},
    {"scr_ofsx", "0", true}, {"scr_ofsy", "0", true}, {"scr_ofsz", "0", true}
};
bool frontend_view_settings_q1_motion_register(qa_cvars *registry, uint64_t owner,
    bool quakeworld, qa_error *error)
{
    for (size_t i = 0; i < sizeof(motion_declarations) / sizeof(*motion_declarations); ++i) {
        if (quakeworld && motion_declarations[i].offset) continue;
        if (!qa_cvars_register(registry, motion_declarations[i].name, motion_declarations[i].initial,
            0, owner, "", error)) return false;
    }
    return !quakeworld || qa_cvars_register(registry, "v_contentblend", "1", 0, owner, "", error);
}
bool frontend_view_settings_q1_motion_owns(const char *name, bool quakeworld)
{
    if (quakeworld && !strcmp(name, "v_contentblend")) return true;
    for (size_t i = 0; i < sizeof(motion_declarations) / sizeof(*motion_declarations); ++i)
        if ((!quakeworld || !motion_declarations[i].offset) && !strcmp(name, motion_declarations[i].name)) return true;
    return false;
}
void frontend_view_settings_q1_motion_bind(const qa_cvars *registry, frontend_q1_motion_refs *refs)
{
    *refs = (frontend_q1_motion_refs){.registry=registry,
        .bob=qa_cvars_resolve(registry,"cl_bob"),
        .bob_cycle=qa_cvars_resolve(registry,"cl_bobcycle"),
        .bob_up=qa_cvars_resolve(registry,"cl_bobup"),
        .roll_speed=qa_cvars_resolve(registry,"cl_rollspeed"),
        .roll_angle=qa_cvars_resolve(registry,"cl_rollangle"),
        .cshift_percent=qa_cvars_resolve(registry,"gl_cshiftpercent"),
        .kick_time=qa_cvars_resolve(registry,"v_kicktime"),
        .kick_roll=qa_cvars_resolve(registry,"v_kickroll"),
        .kick_pitch=qa_cvars_resolve(registry,"v_kickpitch"),
        .idle_scale=qa_cvars_resolve(registry,"v_idlescale"),
        .idle_cycle={qa_cvars_resolve(registry,"v_ipitch_cycle"),
            qa_cvars_resolve(registry,"v_iyaw_cycle"),qa_cvars_resolve(registry,"v_iroll_cycle")},
        .idle_level={qa_cvars_resolve(registry,"v_ipitch_level"),
            qa_cvars_resolve(registry,"v_iyaw_level"),qa_cvars_resolve(registry,"v_iroll_level")},
        .offset={qa_cvars_resolve(registry,"scr_ofsx"),
            qa_cvars_resolve(registry,"scr_ofsy"),qa_cvars_resolve(registry,"scr_ofsz")},
        .contents_blend=qa_cvars_resolve(registry,"v_contentblend")};
}
static bool motion_setting(const frontend_q1_motion_refs *refs, qa_cvar_handle handle, float *out, qa_error *error)
{
    const qa_cvar_view *row = qa_cvars_read(refs->registry, handle);
    if (!row || !isfinite(row->number))
        return fail(error, "Q1 view motion lost its actual finite CLIENT setting");
    *out = (float)row->number; return true;
}
bool frontend_view_settings_q1_motion_sample(const frontend_q1_motion_refs *refs, bool quakeworld,
    frontend_q1_motion_settings *out, qa_error *error)
{
    frontend_q1_motion_settings value = {.contents_blend=true};
    if (!refs || !refs->registry || !out) return fail(error, "Q1 view motion requires its actual registry and output");
    if (!motion_setting(refs, refs->bob, &value.bob, error) ||
        !motion_setting(refs, refs->bob_cycle, &value.bob_cycle, error) ||
        !motion_setting(refs, refs->bob_up, &value.bob_up, error) ||
        !motion_setting(refs, refs->roll_speed, &value.roll_speed, error) ||
        !motion_setting(refs, refs->roll_angle, &value.roll_angle, error) ||
        !motion_setting(refs, refs->cshift_percent, &value.cshift_percent, error) ||
        !motion_setting(refs, refs->kick_time, &value.kick_time, error) ||
        !motion_setting(refs, refs->kick_roll, &value.kick_roll, error) ||
        !motion_setting(refs, refs->kick_pitch, &value.kick_pitch, error) ||
        !motion_setting(refs, refs->idle_scale, &value.idle_scale, error) ||
        !motion_setting(refs, refs->idle_cycle[0], &value.idle_cycle.x, error) ||
        !motion_setting(refs, refs->idle_cycle[1], &value.idle_cycle.y, error) ||
        !motion_setting(refs, refs->idle_cycle[2], &value.idle_cycle.z, error) ||
        !motion_setting(refs, refs->idle_level[0], &value.idle_level.x, error) ||
        !motion_setting(refs, refs->idle_level[1], &value.idle_level.y, error) ||
        !motion_setting(refs, refs->idle_level[2], &value.idle_level.z, error)) return false;
    if (quakeworld) {
        float enabled;
        if (!motion_setting(refs, refs->contents_blend, &enabled, error)) return false;
        value.contents_blend = enabled != 0;
    }
    if (!quakeworld && (!motion_setting(refs, refs->offset[0], &value.offset.x, error) ||
        !motion_setting(refs, refs->offset[1], &value.offset.y, error) ||
        !motion_setting(refs, refs->offset[2], &value.offset.z, error))) return false;
    if (value.bob_cycle <= 0 || value.bob_up <= 0 || value.bob_up >= 1)
        return fail(error, "Q1 bob requires a positive cycle and an up fraction between zero and one");
    *out = value; return true;
}
void frontend_view_q1_damage(const frontend_q1_motion_settings *settings, qa_vec3 origin, qa_vec3 angles,
    uint8_t armor, uint8_t blood, qa_vec3 from, double seconds, frontend_q1_view_motion *state)
{
    float count = (float)(blood * .5 + armor * .5);
    if (count < 10) count = 10;
    state->face_until = seconds + .2;
    state->damage_percent = (int32_t)((float)state->damage_percent + 3 * count);
    if (state->damage_percent > 150) state->damage_percent = 150;
    state->damage_color = armor > blood ? qa_v3(200,100,100) :
        armor ? qa_v3(220,50,50) : qa_v3(255,0,0);
    from = qa_vec_sub(from, origin);
    float length = (float)sqrt(qa_vec_dot(from, from));
    if (length != 0) from = qa_vec_scale(from, 1 / length);
    qa_vec3 axes[3]; frontend_camera_axes(angles, axes);
    state->damage_roll = count * -qa_vec_dot(from, axes[1]) * settings->kick_roll;
    state->damage_pitch = count * qa_vec_dot(from, axes[0]) * settings->kick_pitch;
    state->damage_time = settings->kick_time;
}
void frontend_view_q1_bonus(frontend_q1_view_motion *state)
{ state->bonus_percent = 50; }
bool frontend_view_q1_bonus_commands(frontend_q1_view_motion *state, const char *text, qa_arena *storage, qa_error *error)
{
    if (!state || !text) return fail(error, "Q1 bonus command lost its actual client text");
    for (const char *at = text; *at;) {
        size_t length = strlen(at), size = qa_command_separator(at, length, QA_RULESET_NETQUAKE);
        qa_command_tokens tokens = {0};
        bool okay = qa_command_tokenize_span(at, size, QA_RULESET_NETQUAKE, false, &tokens,
            qa_arena_alloc_callback, storage, error);
        if (okay && tokens.count && strlen(tokens.values[0]) == 2 &&
            (tokens.values[0][0] == 'b' || tokens.values[0][0] == 'B') &&
            (tokens.values[0][1] == 'f' || tokens.values[0][1] == 'F'))
            frontend_view_q1_bonus(state);
        qa_command_tokens_free(&tokens);
        if (!okay) return false;
        at += size < length ? size + 1 : size;
    }
    return true;
}
static bool local_bonus(qa_frontend *f, qa_actor_id actor, const char *commands, qa_error *error)
{
    for (unsigned physical = 0; physical < f->options.seats && !f->options.dedicated; ++physical) {
        uint32_t logical; qa_actor_id admitted;
        if (!frontend_seat_launch_id_read(f, physical, &logical) ||
            !qa_application_player_actor(f->application, logical, &admitted) ||
            !qa_actor_id_equal(actor, admitted)) continue;
        frontend_config_legacy_view source; bool present;
        if (!frontend_config_store_primary_legacy_read(f->config_store, logical, &source, &present, error)) return false;
        if (!present || source.product->family != QA_GAME_Q1) return true;
        frontend_seat *seat = f->seats + physical;
        if (!qa_actor_id_equal(seat->q1_view_actor, actor)) {
            seat->q1_view_motion = (frontend_q1_view_motion){0}; seat->q1_view_actor = actor;
        }
        if (commands) return frontend_view_q1_bonus_commands(&seat->q1_view_motion, commands, &f->frame.storage, error);
        frontend_view_q1_bonus(&seat->q1_view_motion); return true;
    }
    return true;
}
bool frontend_view_q1_local_bonus(qa_frontend *f, qa_actor_id actor, qa_error *error)
{ return local_bonus(f, actor, NULL, error); }
bool frontend_view_q1_local_bonus_commands(qa_frontend *f, qa_actor_id actor, const char *commands, qa_error *error)
{ return local_bonus(f, actor, commands, error); }
qa_vec4 frontend_view_q1_blend(const frontend_q1_motion_settings *settings,
    const frontend_q1_view_motion *state, int32_t contents, bool quakeworld, uint32_t items)
{
    qa_vec3 colors[4] = {{0,0,0}, state->damage_color, {215,186,69}, {0,0,0}};
    int32_t percents[4] = {0, state->damage_percent, state->bonus_percent, 0};
    if (items & 4194304u) { colors[3] = qa_v3(0,0,255); percents[3] = 30; }
    else if (items & 2097152u) { colors[3] = qa_v3(0,255,0); percents[3] = 20; }
    else if (items & 524288u) { colors[3] = qa_v3(100,100,100); percents[3] = 100; }
    else if (items & 1048576u) { colors[3] = qa_v3(255,255,0); percents[3] = 30; }
    if (settings->contents_blend && contents != -1 && (quakeworld || contents != -2)) {
        if (contents == -5) { colors[0] = qa_v3(255,80,0); percents[0] = 150; }
        else if (contents == -4 || (quakeworld && contents == -2)) {
            colors[0] = qa_v3(0,25,5); percents[0] = 150;
        } else { colors[0] = qa_v3(130,80,50); percents[0] = 128; }
    }
    qa_vec3 color = {0}; float alpha = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (settings->cshift_percent == 0) continue;
        float weight = (float)(((float)percents[i] * settings->cshift_percent / 100.0) / 255.0);
        if (weight == 0) continue;
        alpha = alpha + weight * (1 - alpha);
        weight = weight / alpha;
        color.x = color.x * (1 - weight) + colors[i].x * weight;
        color.y = color.y * (1 - weight) + colors[i].y * weight;
        color.z = color.z * (1 - weight) + colors[i].z * weight;
    }
    return (qa_vec4){color.x/255,color.y/255,color.z/255,fmaxf(0,fminf(1,alpha))};
}
const char *frontend_view_q1_face(int32_t health, uint32_t items, double seconds,
    const frontend_q1_view_motion *state)
{
    if ((items & (524288u | 1048576u)) == (524288u | 1048576u)) return "face_inv2";
    if (items & 4194304u) return "face_quad";
    if (items & 524288u) return "face_invis";
    if (items & 1048576u) return "face_invul2";
    static const char *const faces[5][2] = {
        {"face5","face_p5"},{"face4","face_p4"},{"face3","face_p3"},
        {"face2","face_p2"},{"face1","face_p1"}};
    unsigned level = health >= 100 ? 4 : health > 0 ? (unsigned)health / 20 : 0;
    return faces[level][seconds <= state->face_until];
}
bool frontend_view_q1_damage_origin(const double from[3], qa_vec3 *out, qa_error *error)
{
    qa_vec3 value=qa_v3((float)from[0],(float)from[1],(float)from[2]);
    if (!qa_vec_finite(value)) return frontend_fail(error,QA_ERROR_FORMAT,"Nonfinite Q1 Source damage center");
    *out=value; return true;
}
bool frontend_view_q1_local_damage(qa_frontend *f, qa_actor_id actor, uint8_t armor, uint8_t blood,
    qa_vec3 from, qa_error *error)
{
    for (unsigned physical = 0; physical < f->options.seats && !f->options.dedicated; ++physical) {
        uint32_t logical; qa_actor_id admitted;
        if (!frontend_seat_launch_id_read(f, physical, &logical) ||
            !qa_application_player_actor(f->application, logical, &admitted) ||
            !qa_actor_id_equal(actor, admitted)) continue;
        frontend_config_legacy_view source; bool present;
        if (!frontend_config_store_primary_legacy_read(f->config_store, logical, &source, &present, error)) return false;
        if (!present || source.product->family != QA_GAME_Q1) return true;
        qa_body_state body; qa_application_camera_view camera;
        if (!qa_world_body_read(qa_application_world(f->application), actor, &body, error)) return false;
        if (!qa_application_control_camera(f->application, actor, &camera))
            return fail(error, "Q1 damage lost its actual selected player camera");
        if (!camera.has_character || camera.character_family != QA_GAME_Q1) return true;
        bool qw = source.product->edition == QA_EDITION_QUAKEWORLD;
        frontend_q1_motion_settings settings;
        if (!frontend_view_settings_q1_motion_sample(source.motion, qw, &settings, error)) return false;
        frontend_seat *seat = f->seats + physical;
        if (!qa_actor_id_equal(seat->q1_view_actor, actor)) {
            seat->q1_view_motion = (frontend_q1_view_motion){0}; seat->q1_view_actor = actor;
        }
        double seconds;
        if (qw) {
            qa_application_network_qw_source clock;
            if (!qa_application_network_qw_source_read(f->application, &clock, error)) return false;
            seconds = (double)clock.source_time_ns / 1000000000.0;
        } else {
            qa_actor_owner owner;
            qa_application_network_q1_world clock;
            if (!qa_application_provider_owner(f->application,source.descriptor->selection.instance,&owner) ||
                !qa_application_network_q1_world_read(f->application,owner,&clock,error)) return false;
            seconds = clock.seconds;
        }
        frontend_view_q1_damage(&settings, body.origin, qw ? camera.angles : body.angles,
            armor, blood, from, seconds, &seat->q1_view_motion);
        return frontend_config_store_primary_legacy_current(f->config_store, &source) ||
            fail(error, "Q1 damage changed its retained CLIENT settings");
    }
    return true;
}
bool frontend_view_q1_chase(const frontend_q1_view_settings *settings, qa_collision_geometry *geometry,
    qa_trace_scratch *scratch, qa_vec3 eye, qa_vec3 aim_angles, qa_vec3 *origin, qa_vec3 *angles, qa_error *error)
{
    if (!settings || !geometry || !origin || !angles)
        return fail(error, "Q1 chase requires its actual world hull and view");
    qa_vec3 axes[3]; frontend_camera_axes(aim_angles, axes);
    qa_vec3 destination = qa_v3(eye.x - axes[0].x * (float)settings->back + axes[1].x * (float)settings->right,
        eye.y - axes[0].y * (float)settings->back + axes[1].y * (float)settings->right, eye.z);
    destination.z = eye.z + (float)settings->up;
    qa_trace_query query = {.start=eye,.end=qa_vec_add(eye,qa_vec_scale(axes[0],4096)),
        .shape={.kind=QA_SHAPE_POINT},.policy=qa_collision_default_policy(QA_GAME_Q1)};
    query.policy.q1_hull=0;
    qa_trace_result hit;
    if (!qa_collision_trace(geometry, scratch, &query, &hit, error)) return false;
    qa_vec3 stop = qa_vec_sub(hit.end, eye);
    float distance = qa_vec_dot(stop, axes[0]);
    if (distance < 1) distance = 1;
    *origin = destination;
    angles->x = (float)(-atan(stop.z / distance) / 3.14159265358979323846 * 180);
    return true;
}
void frontend_view_q1_motion(const frontend_q1_motion_settings *settings,
    const frontend_q1_motion_input *input, frontend_q1_view_motion *state, frontend_q1_view_pose *out)
{
    if (state->initialized && input->seconds < state->seconds)
        *state = (frontend_q1_view_motion){0};
    if (!state->initialized) state->old_z = input->origin.z;
    double elapsed = input->frame_seconds;
    if (elapsed < 0) elapsed = 0;
    state->seconds = input->seconds; state->initialized = true;
    double remaining = state->damage_percent - elapsed * 150;
    state->damage_percent = remaining <= 0 ? 0 : (int32_t)remaining;
    remaining = state->bonus_percent - elapsed * 100;
    state->bonus_percent = remaining <= 0 ? 0 : (int32_t)remaining;
    float idle_scale = input->intermission ? 1 : settings->idle_scale;
    double idle[3] = {
        idle_scale * sin(input->seconds * settings->idle_cycle.x) * settings->idle_level.x,
        idle_scale * sin(input->seconds * settings->idle_cycle.y) * settings->idle_level.y,
        idle_scale * sin(input->seconds * settings->idle_cycle.z) * settings->idle_level.z};
    if (input->intermission) {
        qa_vec3 angles = qa_v3((float)(input->angles.x + idle[0]),
            (float)(input->angles.y + idle[1]), (float)(input->angles.z + idle[2]));
        *out = (frontend_q1_view_pose){.origin = input->origin, .angles = angles,
            .gun_origin = input->origin, .gun_angles = input->angles};
        return;
    }
    float bob = state->bob;
    if (input->quakeworld && input->spectator) bob = 0;
    else if (!input->quakeworld || input->grounded) {
        if (input->quakeworld) state->bob_seconds += elapsed;
        double seconds = input->quakeworld ? state->bob_seconds : input->seconds;
        float periods = (float)trunc(seconds / settings->bob_cycle);
        float cycle = (float)(seconds - periods * settings->bob_cycle);
        cycle /= settings->bob_cycle;
        if (cycle < settings->bob_up) cycle = (float)(3.14159265358979323846 * cycle / settings->bob_up);
        else cycle = (float)(3.14159265358979323846 + 3.14159265358979323846 *
            (cycle - settings->bob_up) / (1.0 - settings->bob_up));
        bob = (float)(sqrt(input->velocity.x * input->velocity.x + input->velocity.y * input->velocity.y) * settings->bob);
        bob = (float)(bob * .3 + bob * .7 * sin(cycle));
        if (bob > 4) bob = 4; else if (bob < -7) bob = -7;
        state->bob = bob;
    }
    qa_vec3 axes[3]; frontend_camera_axes(input->quakeworld ? input->angles : input->entity_angles, axes);
    float side = -qa_vec_dot(input->velocity, axes[1]);
    float sign = side < 0 ? -1 : 1;
    side = fabsf(side);
    if (side < settings->roll_speed) side = side * settings->roll_angle / settings->roll_speed;
    else side = settings->roll_angle;
    qa_vec3 camera_angles = input->angles;
    camera_angles.z += side * sign;
    if (state->damage_time > 0) {
        camera_angles.z += state->damage_time / settings->kick_time * state->damage_roll;
        camera_angles.x += state->damage_time / settings->kick_time * state->damage_pitch;
        state->damage_time -= (float)elapsed;
    }
    if (!input->quakeworld && input->dead) camera_angles.z = 80;
    camera_angles.x = (float)(camera_angles.x + idle[0]);
    camera_angles.y = (float)(camera_angles.y + idle[1]);
    camera_angles.z = (float)(camera_angles.z + idle[2]);
    float bias = input->quakeworld ? 1.0f / 16 : 1.0f / 32;
    qa_vec3 origin = input->origin;
    origin.z += input->quakeworld ? bob : input->view_height + bob;
    origin.x += bias; origin.y += bias; origin.z += bias;
    if (input->quakeworld) origin.z += input->view_height;
    qa_vec3 aim = input->angles;
    if (!input->quakeworld) aim.z = input->entity_angles.z;
    frontend_camera_axes(aim, axes);
    if (!input->quakeworld) {
        origin = qa_vec_add(origin, qa_vec_add(qa_vec_add(qa_vec_scale(axes[0], settings->offset.x),
            qa_vec_scale(axes[1], -settings->offset.y)), qa_vec_scale(axes[2], settings->offset.z)));
        origin.x = fmaxf(input->origin.x - 14, fminf(input->origin.x + 14, origin.x));
        origin.y = fmaxf(input->origin.y - 14, fminf(input->origin.y + 14, origin.y));
        origin.z = fmaxf(input->origin.z - 22, fminf(input->origin.z + 30, origin.z));
    } else if (input->dead) camera_angles.z = 80;
    qa_vec3 gun = input->origin; gun.z += input->quakeworld ? 22 : input->view_height;
    gun.x = (float)(gun.x + axes[0].x * bob * .4);
    gun.y = (float)(gun.y + axes[0].y * bob * .4);
    gun.z = (float)(gun.z + axes[0].z * bob * .4);
    gun.z += bob;
    if (input->view_size == 110 || input->view_size == 90) gun.z += 1;
    else if (input->view_size == 100) gun.z += 2;
    else if (input->view_size == 80) gun.z += .5f;
    /* Original CalcGunAngle's yaw/pitch deltas subtract themselves and stay
     * zero. Convert its backward model pitch to the shared geometric axes. */
    qa_vec3 gun_angles = qa_v3((float)(camera_angles.x + idle[0]),
        (float)(camera_angles.y - idle[1]), (float)(input->angles.z - idle[2]));
    if (input->quakeworld) camera_angles.x += input->punch.x;
    else camera_angles = qa_vec_add(camera_angles, input->punch);
    if (input->grounded && input->origin.z - state->old_z > 0) {
        state->old_z += (float)elapsed * 80;
        if (state->old_z > input->origin.z) state->old_z = input->origin.z;
        if (input->origin.z - state->old_z > 12) state->old_z = input->origin.z - 12;
        origin.z += state->old_z - input->origin.z; gun.z += state->old_z - input->origin.z;
    } else state->old_z = input->origin.z;
    *out = (frontend_q1_view_pose){.origin = origin, .angles = camera_angles, .gun_origin = gun, .gun_angles = gun_angles};
}

static bool prepare(frontend_view_settings *owner,const qa_launch_snapshot *candidate,
    const qa_application_client_preparation *client,
    const qa_cvars_edit *edit,frontend_view_transition transition,frontend_view_preparation **out,qa_error *e)
{
    if (!current(owner) || owner->preparation || owner->notifying || !out || *out ||
        (unsigned)transition>FRONTEND_VIEW_BORROWED ||
        (transition==FRONTEND_VIEW_INITIAL && owner->published) ||
        (transition==FRONTEND_VIEW_REPLACEMENT && !owner->published) ||
        !(client?(qa_application_client_prepare_associated(owner->application,client) &&
            qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_RESOURCES)):
            qa_application_startup_resource_phase(owner->application,candidate)) ||
        !qa_cvars_edit_returned_is(edit,owner->registry))
        return fail(e,"View preparation requires its real publication transition and canonical ticket");
    const qa_cvar_view *canonical=qa_cvars_edit_canonical_record(edit,"fov");
    const qa_cvar_view *row=canonical?qa_cvars_edit_find(edit,canonical->name):NULL,*previous=record(owner);
    double number;
    if (!row || !previous || strcmp(row->name,previous->name))
        return fail(e,"View preparation lost its physical canonical fov record");
    if (!decimal(row->value,&number,e)) return false;
    frontend_view_preparation *held=calloc(1,sizeof(*held));
    if (!held) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining candidate view preference");
    held->parent=owner; held->candidate=candidate; held->client=client; held->edit=edit; held->transition=transition;
    held->value=copy(row->value,e); held->previous_number=owner->value;
    if (!held->value) { free(held); return false; }
    held->number=number; held->modification_count=row->modification_count;
    held->previous_explicit=owner->explicit_override; held->previous_published=owner->published;
    held->notify=transition==FRONTEND_VIEW_BORROWED ||
        (transition==FRONTEND_VIEW_INITIAL?number!=90:number!=owner->value);
    held->explicit_override=transition==FRONTEND_VIEW_INITIAL?number!=90:
        owner->explicit_override || held->notify;
    owner->preparation=held; *out=held; return true;
}
bool frontend_view_settings_prepare(frontend_view_settings *owner,const qa_launch_snapshot *candidate,
    const qa_cvars_edit *edit,frontend_view_transition transition,frontend_view_preparation **out,qa_error *e)
{ return prepare(owner,candidate,NULL,edit,transition,out,e); }
bool frontend_view_settings_prepare_client(frontend_view_settings *owner,
    const qa_application_client_preparation *client,const qa_cvars_edit *edit,
    frontend_view_transition transition,frontend_view_preparation **out,qa_error *e)
{
    if (!client) return fail(e,"CLIENT view preparation requires its actual physical token");
    return prepare(owner,NULL,client,edit,transition,out,e);
}
bool frontend_view_settings_ready_is(const frontend_view_preparation *held)
{
    frontend_view_settings *owner=held?held->parent:NULL;
    if (!current(owner) || owner->preparation!=held || held->published || owner->notifying ||
        owner->explicit_override!=held->previous_explicit || owner->published!=held->previous_published ||
        !qa_cvars_edit_ready_is(held->edit) || qa_cvars_edit_registry(held->edit)!=owner->registry ||
        !(held->client?(qa_application_client_prepare_associated(owner->application,held->client) &&
            (qa_application_client_prepare_phase_is(held->client,QA_CLIENT_PREPARE_RESOURCES) ||
                qa_application_client_prepare_entered(held->client,QA_CLIENT_PREPARE_CONSUMING))):
            (qa_application_startup_resource_phase_associated(owner->application,held->candidate) ||
                qa_application_startup_publication_consuming(owner->application,held->candidate)))) return false;
    const qa_cvar_view *canonical=qa_cvars_edit_canonical_record(held->edit,"fov");
    const qa_cvar_view *row=canonical?qa_cvars_edit_find(held->edit,canonical->name):NULL,*previous=record(owner);
    return row && previous && !strcmp(row->name,previous->name) &&
        row->modification_count==held->modification_count && !strcmp(row->value,held->value) &&
        owner->value==held->previous_number;
}
void frontend_view_settings_publish(frontend_view_preparation *held)
{
    if (!frontend_view_settings_ready_is(held)) return;
    frontend_view_settings *owner=held->parent;
    owner->value=held->number; owner->modification_count=held->modification_count;
    owner->explicit_override=held->explicit_override; owner->published=true;
    held->published=true; held->edit=NULL;
}
bool frontend_view_settings_apply(frontend_view_preparation *held,qa_error *e)
{
    frontend_view_settings *owner=held?held->parent:NULL;
    if (!current(owner) || owner->preparation!=held || !held->published || !value_current(owner))
        return fail(e,"View recipients require their actual published scalar");
    if (held->applied) return true;
    held->applied=true;
    if (held->notify) {
        owner->notifying=true;
        bool ok=owner->changed(owner->context,owner->value,e);
        owner->notifying=false;
        if (!ok) return (e && e->code!=QA_OK)?false:
            fail(e,"Published view recipient rejected its actual preference");
    }
    return true;
}
static void release(frontend_view_preparation *held)
{
    held->parent->preparation=NULL;
    free(held->value); free(held);
}
bool frontend_view_settings_finish(frontend_view_preparation **in,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_view_preparation *held=*in;
    if (!current(held->parent) || !held->published || !held->applied ||
        !value_current(held->parent) || !qa_cvars_observer_idle(held->parent->registry))
        return fail(e,"View retirement retains its actual published observer drain");
    release(held); *in=NULL; return true;
}
bool frontend_view_settings_abort(frontend_view_preparation **in,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_view_preparation *held=*in;
    if (!current(held->parent) || held->published || held->parent->notifying ||
        !qa_cvars_edit_abort_is(held->edit,held->parent->registry))
        return fail(e,"View cancellation requires its returned unpublished scalar");
    release(held); *in=NULL; return true;
}
static bool fields(qa_source_save_io *io,bool *published,bool *explicit_override,double *value,uint64_t *revision)
{
    uint8_t magic[4]={'Q','F','V','S'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFVS",4) &&
        qa_source_save_bool(io,published) &&
        qa_source_save_bool(io,explicit_override) && qa_source_save_f64(io,value) &&
        qa_source_save_u64(io,revision) && isfinite(*value) && *value>=60 && *value<=160;
}
bool frontend_view_settings_checkpoint(const frontend_view_settings *owner,qa_buffer *out,qa_error *e)
{
    if (!value_current(owner) || owner->preparation || owner->notifying ||
        !out || out->data || out->size || !qa_cvars_observer_idle(owner->registry))
        return fail(e,"View capture requires its complete actual published owner");
    bool published=owner->published,explicit_override=owner->explicit_override;
    double value=owner->value; uint64_t revision=owner->modification_count;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e) &&
        fields(&io,&published,&explicit_override,&value,&revision) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_view_settings_restore(frontend_view_settings *owner,qa_bytes bytes,qa_error *e)
{
    if (!current(owner) || owner->preparation || owner->published || owner->notifying ||
        !qa_cvars_observer_idle(owner->registry)) return fail(e,"View import requires its detached fresh callbacks");
    bool published=false,explicit_override=false; double value=0; uint64_t revision=0;
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e) &&
        fields(&io,&published,&explicit_override,&value,&revision) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    const qa_cvar_view *row=ok?record(owner):NULL; double actual=0;
    if (!row || row->modification_count!=revision || !decimal(row->value,&actual,e) || actual!=value)
        return fail(e,"View import disagrees with its genuine imported canonical scalar");
    owner->value=value; owner->modification_count=revision;
    owner->published=published; owner->explicit_override=explicit_override; return true;
}
static void destroy(frontend_view_settings **in)
{
    frontend_view_settings *owner=*in;
    qa_cvars_unobserve(owner->registry,owner->observer);
    qa_cvars_unbind(owner->registry,qa_cvars_canonical_name(owner->registry,"fov"),QA_FRONTEND_COMMAND_OWNER);
    if (owner->installed && *owner->installed==owner) *owner->installed=NULL;
    free(owner); *in=NULL;
}
bool frontend_view_settings_destroy(frontend_view_settings **in,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_view_settings *owner=*in;
    if (!current(owner) || owner->preparation || owner->notifying || !qa_cvars_observer_idle(owner->registry))
        return fail(e,"View destruction retains its actual scalar and observer callbacks");
    destroy(in); return true;
}
bool frontend_view_settings_shutdown(frontend_view_settings **in,
    const qa_application_engine_shutdown *loan,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_view_settings *owner=*in;
    qa_console *console=NULL; qa_cvars *registry=NULL;
    if (!loan || !owner->frontend || owner->frontend->application!=owner->application ||
        qa_application_engine_shutdown_owner(loan)!=owner->application ||
        !qa_application_engine_shutdown_read(loan,&console,&registry,e) || registry!=owner->registry ||
        !owner->installed || *owner->installed!=owner || owner->preparation || owner->notifying ||
        !record(owner) || !qa_cvars_observer_idle(registry))
        return fail(e,"View shutdown retains its exact detached ENGINE callback owner");
    destroy(in); return true;
}
bool frontend_view_settings_rebind(frontend_view_settings *owner,qa_frontend *f,
    frontend_view_settings **slot,void *context,qa_error *e)
{
    if (!owner || !f || !f->application || !slot || (*slot && *slot!=owner) ||
        !owner->installed || *owner->installed!=owner || owner->preparation || owner->notifying ||
        qa_application_cvars(f->application)!=owner->registry || !record(owner) ||
        !qa_cvars_observer_idle(owner->registry))
        return fail(e,"View rebind requires its same genuine published canonical registry");
    if (owner->installed!=slot) *owner->installed=NULL;
    owner->frontend=f; owner->application=f->application; owner->installed=slot; owner->context=context;
    view_cvars_bind(owner);
    *slot=owner; return true;
}
