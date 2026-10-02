#include "remote_unified_private.h"
#include "remote_unified_render.h"
#include "view_settings.h"
#include "legacy_render_policy.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct unified_render_model {
    frontend_unified_model media;
    qa_scene_model_input input;
    const qa_product *product;
    char *path;
    qa_actor_id actor;
    qa_vec3 origin, angles;
    qa_vec3 previous_origin;
    float scale;
    bool visible, has_previous_origin;
} unified_render_model;
struct frontend_unified_render {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    qa_unified_document *frame;
    qa_buffer area_bits;
    unified_render_model *models;
    size_t model_count;
    qa_hud *hud;
    qa_hud_value vitals[3];
    char *ammo_label;
    qa_vec3 origin, angles, kick;
    float height;
    double seconds, field_of_view;
    bool explicit_fov, source_view_offset, busy;
};
static qa_json_id field(const qa_json_document *j, qa_json_id id, const char *name)
{ return qa_json_get(j, id, name); }
static bool scalar(const qa_unified_document *d, qa_json_id id, double *out, qa_error *e)
{
    return (qa_unified_document_number(d, id, out, e) && isfinite(*out)) ||
        frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified render scalar is not finite");
}
static bool real(const qa_unified_document *d, qa_json_id id, float *out, qa_error *e)
{
    double value;
    if (!scalar(d, id, &value, e)) return false;
    if (fabs(value) > FLT_MAX) return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified render scalar exceeds float storage");
    *out = (float)value; return true;
}
static bool vector(const qa_unified_document *d, qa_json_id id, qa_vec3 *out, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d);
    return real(d, field(j,id,"x"), &out->x,e) && real(d,field(j,id,"y"),&out->y,e) && real(d,field(j,id,"z"),&out->z,e);
}
static bool word(const qa_unified_document *d, qa_json_id id, uint32_t *out, qa_error *e)
{
    double v;
    if (!scalar(d,id,&v,e)) return false;
    if (v < 0 || v > UINT32_MAX || trunc(v) != v)
        return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified render index exceeds its native domain");
    *out=(uint32_t)v; return true;
}
static bool hud_read(void *context, const qa_hud_frame *frame, qa_hud_data *out, qa_error *error)
{
    frontend_unified_render *r=context;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r->replica);
    if (!r->busy || !d || frame->seat!=d->physical_seat)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified HUD changed its received physical seat");
    *out=(qa_hud_data){.vitals=r->vitals,.vital_count=r->ammo_label?3:2,.source_vitals=true,
        .crosshair_visible=true,.crosshair_color={1,1,1,1}};
    return true;
}
static bool model_read(frontend_unified_render *r, qa_json_id id, unified_render_model *m, qa_error *e)
{
    const qa_unified_document *d=r->frame;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_buffer content={0},path={0};
    qa_json_id family=field(j,id,"family"),wire=field(j,id,"actor");
    uint64_t slot,generation;
    qa_scene_family kind;
    if (qa_json_string_equal(j,family,"q1")) kind=QA_SCENE_Q1;
    else if (qa_json_string_equal(j,family,"q2")) kind=QA_SCENE_Q2;
    else if (qa_json_string_equal(j,family,"q3")) kind=QA_SCENE_Q3;
    else return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified model has no admitted rendering family");
    bool okay=qa_json_string(j,field(j,id,"content"),&content,e) && qa_json_string(j,field(j,id,"path"),&path,e) &&
        qa_json_u64(j,field(j,wire,"slot"),&slot,e) && slot<=UINT32_MAX &&
        qa_json_u64(j,field(j,wire,"generation"),&generation,e) &&
        frontend_remote_unified_actor(r->replica,(uint32_t)slot,generation,&m->actor,e) &&
        vector(d,field(j,id,"origin"),&m->origin,e) && vector(d,field(j,id,"angles"),&m->angles,e) &&
        real(d,field(j,id,"scale"),&m->scale,e) && qa_json_bool(j,field(j,id,"visible"),&m->visible,e);
    double skin=0,flags=0,current=0,old=0;
    if (okay) okay=scalar(d,field(j,id,"frame"),&current,e) && scalar(d,field(j,id,"oldFrame"),&old,e) &&
        scalar(d,field(j,id,"skin"),&skin,e) && scalar(d,field(j,id,"renderFlags"),&flags,e);
    if (okay && (current<INT32_MIN || current>UINT32_MAX || old<INT32_MIN || old>UINT32_MAX || trunc(current)!=current || trunc(old)!=old))
        okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified model frames exceed actual source words");
    if (okay) { m->input.frame=current<0?0:(uint32_t)current;
        m->input.old_frame=old<0?m->input.frame:(uint32_t)old; }
    if (okay && (flags<INT32_MIN || flags>UINT32_MAX || trunc(flags)!=flags || skin>UINT32_MAX || skin<INT32_MIN || trunc(skin)!=skin))
        okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified model flags or skin exceed actual source words");
    qa_scene_image_options images={.family=kind,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
    uint8_t translation[256];
    qa_json_id colors=field(j,id,"playerColors");
    if (okay && colors!=QA_JSON_NONE) {
        uint32_t top,bottom;
        okay=kind==QA_SCENE_Q1 && word(d,field(j,colors,"top"),&top,e) && top<16 &&
            word(d,field(j,colors,"bottom"),&bottom,e) && bottom<16;
        if (okay) { for (unsigned i=0;i<256;++i) translation[i]=(uint8_t)i;
            for (unsigned i=0;i<16;++i) { unsigned t=top*16,b=bottom*16;
                translation[16+i]=(uint8_t)(t<128?t+i:t+15-i);
                translation[96+i]=(uint8_t)(b<128?b+i:b+15-i); }
            images.translation=(qa_bytes){translation,sizeof(translation)}; }
    }
    qa_scene_resources *bank; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *sounds;
    qa_vfs *files;
    if (okay) okay=qa_executable_recipe_content(frontend_remote_unified_recipe(r->replica),
        (const char *)content.data,&files,&m->product,e) &&
        frontend_unified_media_bank(r->media,(const char *)content.data,&bank,&materials,&fonts,&sounds,e) &&
        frontend_unified_media_model(r->media,(const char *)content.data,(const char *)path.data,kind,&images,&m->media,e);
    if (okay) {
        m->path=(char *)path.data; path=(qa_buffer){0};
        m->input.family=kind; m->input.skin=skin<0?0:(uint32_t)skin;
        m->input.flags=flags<0?(uint32_t)(int32_t)flags:(uint32_t)flags;
        m->input.entity=m->actor.slot; m->input.material_library=materials; m->input.source_path=m->path;
        m->input.color=(qa_scene_vec4){1,1,1,1}; m->input.seconds=r->seconds;
        m->input.has_milliseconds=true; m->input.milliseconds=(int64_t)(r->seconds*1000);
        qa_json_id alpha=field(j,id,"alpha");
        if (alpha!=QA_JSON_NONE) okay=real(d,alpha,&m->input.color.w,e);
        qa_json_id weapon=field(j,id,"viewWeapon");
        if (okay && weapon!=QA_JSON_NONE) okay=qa_json_bool(j,weapon,&m->input.view_model,e);
        qa_json_id previous=field(j,id,"previousOrigin"),lerp=field(j,id,"backLerp");
        if (okay && previous!=QA_JSON_NONE) { okay=vector(d,previous,&m->previous_origin,e); m->has_previous_origin=okay; }
        if (okay && lerp!=QA_JSON_NONE) okay=real(d,lerp,&m->input.back_lerp,e);
        qa_json_id custom=field(j,id,"skinPath");
        if (okay && custom!=QA_JSON_NONE) { qa_buffer name={0};
            okay=qa_json_string(j,custom,&name,e) && qa_material_register(materials,(const char *)name.data,&images,
                false,&m->input.custom_material,e); qa_buffer_free(&name); }
    }
    qa_buffer_free(&content); qa_buffer_free(&path); return okay;
}
bool frontend_unified_render_create(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_media *media,const qa_unified_document *frame,frontend_unified_render **out,qa_error *e)
{
    if (!f || !replica || !media || !frame || !out || *out ||
        qa_unified_document_type(frame)!=QA_UNIFIED_FRAME_DOCUMENT || !frontend_unified_media_current(media))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified frame preparation lost its actual replica media");
    frontend_unified_render *r=calloc(1,sizeof(*r));
    if (!r) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining unified received render frame");
    r->frontend=f; r->replica=replica; r->media=media;
    bool okay=frontend_unified_clone(frame,&r->frame,e);
    const qa_json_document *j=qa_unified_document_json(frame); qa_json_id root=qa_unified_document_root(frame);
    qa_json_id output=field(j,root,"output"),snapshot=field(j,output,"snapshot"),clock=field(j,field(j,snapshot,"frame"),"time");
    double clock_value=0;
    if (okay) okay=scalar(frame,field(j,clock,"value"),&clock_value,e);
    if (okay) { r->seconds=qa_json_string_equal(j,field(j,clock,"kind"),"milliseconds")?clock_value/1000:clock_value;
        if (r->seconds<0 || r->seconds*1e9>=18446744073709551616.0)
            okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified received clock exceeds renderer time storage"); }
    qa_json_id area=field(j,field(j,snapshot,"scene"),"areaBits");
    if (okay && qa_json_type(j,area)!=QA_JSON_NULL)
        okay=qa_unified_document_bytes(frame,area,&r->area_bits,e);
    qa_json_id player=field(j,root,"player"),view=field(j,player,"view"),ui=field(j,player,"ui");
    if (okay) okay=vector(frame,field(j,view,"origin"),&r->origin,e) && vector(frame,field(j,view,"angles"),&r->angles,e) &&
        real(frame,field(j,view,"viewHeight"),&r->height,e);
    r->source_view_offset=field(j,view,"clientViewOffsetDelta")!=QA_JSON_NONE;
    qa_json_id kick=field(j,view,"kickAngles"),fov=field(j,view,"fieldOfView");
    if (okay && kick!=QA_JSON_NONE) okay=vector(frame,kick,&r->kick,e);
    if (okay && fov!=QA_JSON_NONE) { okay=scalar(frame,fov,&r->field_of_view,e); r->explicit_fov=true; }
    if (okay) okay=scalar(frame,field(j,ui,"health"),&r->vitals[0].value,e);
    r->vitals[0].label="Health"; r->vitals[0].warning=r->vitals[0].value<=25;
    r->vitals[1].label="Armor";
    qa_json_id armor=field(j,field(j,ui,"armor"),"regular"),points=field(j,armor,"points");
    if (okay && points!=QA_JSON_NONE) okay=scalar(frame,points,&r->vitals[1].value,e);
    qa_json_id ammo=field(j,ui,"ammo");
    if (okay && qa_json_type(j,ammo)==QA_JSON_OBJECT) {
        qa_json_id status=field(j,ui,"weaponStatus"); qa_buffer label={0};
        qa_json_id label_field=qa_json_type(j,status)==QA_JSON_OBJECT?field(j,status,"label"):field(j,ammo,"item");
        okay=qa_json_string(j,label_field,&label,e) && scalar(frame,field(j,ammo,"count"),&r->vitals[2].value,e);
        if (okay) { r->ammo_label=(char *)label.data; label=(qa_buffer){0};
            r->vitals[2].label=r->ammo_label; r->vitals[2].warning=qa_json_string_equal(j,field(j,ui,"arsenalWarning"),"empty") ||
                qa_json_string_equal(j,field(j,ui,"arsenalWarning"),"low"); }
        qa_buffer_free(&label);
    }
    qa_json_id models=field(j,root,"models"); size_t count=qa_json_size(j,models);
    if (okay && count>SIZE_MAX/sizeof(*r->models)) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Unified model roster exceeds storage");
    if (okay && count) { r->models=calloc(count,sizeof(*r->models)); okay=r->models!=NULL;
        if (!okay) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received unified model bindings"); }
    for (size_t i=0;okay && i<count;++i) { r->model_count=i+1; okay=model_read(r,qa_json_at(j,models,i),r->models+i,e); }
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica);
    if (okay) okay=domain && qa_hud_create(&(qa_hud_options){.ui=f->seats[domain->physical_seat].ui,
        .application=domain->application,.seat=domain->physical_seat,.context=r,.read=hud_read},&r->hud,e);
    if (!okay) { (void)frontend_unified_render_destroy(&r,NULL); return false; }
    *out=r; return true;
}
bool frontend_unified_render_draw(frontend_unified_render *r,const frontend_unified_prediction_view *predicted,
    float stereo,qa_audio_listener *listener,qa_error *e)
{
    if (!r || r->busy || !listener || !isfinite(stereo) || !frontend_unified_media_current(r->media))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified draw lost its actual received frame");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r->replica);
    qa_actor_id player; uint32_t source;
    if (!d || !frontend_remote_unified_player(r->replica,&player,&source)) return false;
    double fov=r->field_of_view; bool override;
    if (!r->explicit_fov && !frontend_view_settings_read(r->frontend->view_settings,&fov,&override))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified camera lost its real published FOV preference");
    qa_scene_view view={.viewport=frontend_viewport(r->frontend,d->physical_seat),.origin=r->origin,
        .clear_depth=true,.depth=1,.seat=d->physical_seat};
    qa_vec3 angles=r->angles; float height=r->height;
    bool predicting=predicted && (predicted->status==FRONTEND_UNIFIED_PREDICTION_ACTIVE ||
        predicted->status==FRONTEND_UNIFIED_PREDICTION_DISABLED);
    if (predicting) {
        if (!qa_actor_id_equal(predicted->actor,player)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified predicted camera changed player");
        view.origin=qa_vec_add(view.origin,predicted->origin_shift); angles=predicted->view_angles;
        if (!r->source_view_offset) height=predicted->view_height;
    }
    view.origin.z+=height;
    frontend_camera_axes(angles,view.axis); qa_vec3 local[3],basis[3]; memcpy(basis,view.axis,sizeof(basis));
    frontend_camera_axes(r->kick,local);
    for (unsigned i=0;i<3;++i) view.axis[i]=qa_vec_add(qa_vec_add(qa_vec_scale(basis[0],local[i].x),
        qa_vec_scale(basis[1],local[i].y)),qa_vec_scale(basis[2],local[i].z));
    view.origin=qa_vec_add(view.origin,qa_vec_scale(view.axis[1],stereo));
    if (!(fov>0 && fov<180) || !view.viewport.width || !view.viewport.height)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified received camera has no finite projection");
    float vertical=2*atanf(tanf((float)fov*.008726646259971648f)*(float)view.viewport.height/view.viewport.width)*57.29577951308232f;
    view.projection=qa_scene_projection((float)fov,vertical,4,16384);
    r->busy=true;
    qa_scene_world_input world={.view=view,.seconds=r->seconds,.milliseconds=(int64_t)(r->seconds*1000),.identity_light=1};
    world.visible_areas=r->area_bits.data; world.visible_area_bytes=r->area_bits.size;
    float q1[256]; qa_vec3 q2[256]; for (size_t i=0;i<256;++i) { q1[i]=256; q2[i]=qa_v3(1,1,1); }
    const qa_json_document *j=qa_unified_document_json(r->frame); qa_json_id root=qa_unified_document_root(r->frame);
    qa_json_id scene=field(j,field(j,field(j,root,"output"),"snapshot"),"scene"),styles=field(j,scene,"lightStyles");
    bool okay=true;
    for (size_t i=0;okay && i<qa_json_size(j,styles);++i) { qa_json_id row=qa_json_at(j,styles,i); uint32_t index;
        okay=word(r->frame,field(j,row,"style"),&index,e) && index<256;
        if (okay && qa_json_string_equal(j,field(j,row,"kind"),"q1")) okay=real(r->frame,field(j,row,"value"),q1+index,e);
        else if (okay) okay=vector(r->frame,field(j,row,"rgb"),q2+index,e); }
    world.q1_styles=q1; world.q2_styles=q2; world.style_count=256;
    if (okay) okay=qa_scene_frame_emit(&r->frontend->frame,&(qa_scene_command){.kind=QA_SCENE_COMMAND_VIEW,.data.view=view},e) &&
        qa_scene_world_submit(frontend_unified_media_world(r->media),&world,&r->frontend->frame,e);
    for (size_t i=0;okay && i<r->model_count;++i) {
        unified_render_model *m=r->models+i;
        float scale=m->input.family==QA_SCENE_Q2 && m->scale==0?1:m->scale;
        if (!m->visible || m->input.color.w<=0 || scale==0 || (qa_actor_id_equal(m->actor,player) && !m->input.view_model)) continue;
        qa_scene_model_input input=m->input; input.view=view;
        qa_vec3 position=m->origin,previous=m->has_previous_origin?m->previous_origin:position;
        if (predicting && qa_actor_id_equal(m->actor,player)) {
            position=qa_vec_add(position,predicted->origin_shift); previous=qa_vec_add(previous,predicted->origin_shift);
        }
        if (input.view_model && input.family==QA_SCENE_Q1) { position.z+=2; previous.z+=2; }
        qa_model_transform_identity(&input.transform); qa_vec3 axes[3]; frontend_camera_axes(m->angles,axes);
        input.transform.origin[0]=position.x;input.transform.origin[1]=position.y;input.transform.origin[2]=position.z;
        for (unsigned a=0;a<3;++a) { input.transform.axes[a][0]=axes[a].x; input.transform.axes[a][1]=axes[a].y;
            input.transform.axes[a][2]=axes[a].z; input.transform.scale[a]=scale; }
        input.previous_origin=previous; input.ambient=qa_v3(1,1,1); input.identity_light=1;
        if (m->media.brush_world) okay=qa_scene_world_submit_model(m->media.brush_world,m->media.inline_model,
            &input.transform,&world,m->actor.slot,input.color,&r->frontend->frame,e);
        else {
            qa_scene_world_sample_light(frontend_unified_media_world(r->media),position,&input.ambient,&input.directed,&input.light_direction);
            okay=frontend_legacy_model_input(r->frontend,m->product->id,frontend_unified_media_world(r->media),&world,&input,e) &&
                qa_scene_model_submit(m->media.scene,&input,&r->frontend->frame,e);
        }
    }
    if (okay) okay=qa_scene_frame_finish(&r->frontend->frame,&view,&world.fog,e);
    if (okay) okay=qa_hud_draw(r->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,
        .time_ns=(uint64_t)(r->seconds*1e9),.viewport=view.viewport,.safe_area=view.viewport,.scale=1,.visible=true},&r->frontend->frame,e);
    if (okay) { *listener=(qa_audio_listener){.seat=d->physical_seat,.actor=player.slot,.origin=view.origin,.gain=1};
        memcpy(listener->axis,view.axis,sizeof(view.axis)); }
    r->busy=false; return okay;
}
bool frontend_unified_render_destroy(frontend_unified_render **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_render *r=*slot;
    if (r->busy || (r->hud && !qa_hud_idle(r->hud)))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified received render frame still has callbacks");
    if (r->hud && !qa_hud_destroy(r->hud,e)) return false;
    for (size_t i=0;i<r->model_count;++i) free(r->models[i].path);
    free(r->models); free(r->ammo_label); qa_buffer_free(&r->area_bits);
    qa_unified_document_destroy(r->frame); free(r); *slot=NULL; return true;
}
