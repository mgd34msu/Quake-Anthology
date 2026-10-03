#include "remote_unified_private.h"
#include "remote_unified_render.h"
#include "view_settings.h"
#include "legacy_render_policy.h"
#include "remote_unified_render_save.h"
#include "remote_unified_save.h"
#include "remote_unified_material_movies_bridge.h"
#include "save_private.h"
#include "material_movies.h"

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
    bool source_client,submitted;
    uint32_t source_provider;
    char *source_instance;
    uint64_t submitted_cycle;
    bool equipment,equipment_slot;
    uint32_t equipment_provider;
    char *equipment_instance;
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
    qa_scene_vec4 blend,damage_blend;
    float height;
    double seconds, field_of_view;
    bool explicit_fov, source_view_offset, busy;
    bool has_blend,has_damage_blend;
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
static bool render_frame_current(const frontend_unified_render *);
static bool presentation_source(const frontend_unified_render *r,qa_json_id value,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(r->frame);
    qa_json_id source=field(j,value,"source"); uint32_t provider;
    if(qa_json_type(j,value)!=QA_JSON_OBJECT || qa_json_type(j,source)!=QA_JSON_OBJECT ||
        !word(r->frame,field(j,source,"provider"),&provider,e) || !provider)return false;
    const qa_executable_recipe *recipe=frontend_remote_unified_recipe(r->replica);
    for(size_t i=0;i<qa_executable_recipe_provider_count(recipe);++i) {
        const qa_recipe_provider *row=qa_executable_recipe_provider(recipe,i);
        if(row->source_owner==provider && row->selection.runtime==QA_PROGRAM_QUAKEC && row->declaration &&
            qa_json_string_equal(j,field(j,source,"instance"),row->selection.instance))return true;
    }
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared QC output lacks its admitted Source identity");
}
static bool client_presentation_read(const frontend_unified_render *r,qa_actor_id actor,
    qa_application_camera_view *camera,qa_hud_value vitals[2],bool *has_view,bool *has_vitals,qa_error *e)
{
    if(!r || !r->frame || !camera || !vitals || !has_view || !has_vitals)return false;
    *has_view=*has_vitals=false;
    const qa_json_document *j=qa_unified_document_json(r->frame);
    qa_json_id value=field(j,field(j,qa_unified_document_root(r->frame),"player"),"clientPresentation");
    if(value==QA_JSON_NONE)return true;
    qa_json_id recipient=field(j,value,"recipient");uint64_t slot,generation;qa_actor_id actual;
    if(qa_json_type(j,value)!=QA_JSON_OBJECT || qa_json_type(j,recipient)!=QA_JSON_OBJECT ||
        !qa_json_u64(j,field(j,recipient,"slot"),&slot,e) || slot>UINT32_MAX ||
        !qa_json_u64(j,field(j,recipient,"generation"),&generation,e) ||
        !frontend_remote_unified_actor(r->replica,(uint32_t)slot,generation,&actual,e) ||
        !qa_actor_id_equal(actual,actor))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared QC output changed its full received player");
    qa_json_id hud=field(j,value,"hud"),view=field(j,value,"view");
    if(hud==QA_JSON_NONE || view==QA_JSON_NONE)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared QC output omits its HUD or view admission");
    if(qa_json_type(j,hud)!=QA_JSON_NULL) {
        double health,armor;
        if(!presentation_source(r,hud,e) || !scalar(r->frame,field(j,hud,"health"),&health,e) ||
            !scalar(r->frame,field(j,hud,"armor"),&armor,e))return false;
        vitals[0]=(qa_hud_value){.label="Health",.value=health,.warning=health<=25};
        vitals[1]=(qa_hud_value){.label="Armor",.value=armor}; *has_vitals=true;
    }
    if(qa_json_type(j,view)!=QA_JSON_NULL) {
        qa_application_camera_view v={.actor=actor,.cutscene=true};
        if(!presentation_source(r,view,e) || !vector(r->frame,field(j,view,"origin"),&v.origin,e) ||
            !vector(r->frame,field(j,view,"angles"),&v.angles,e) ||
            !real(r->frame,field(j,view,"viewHeight"),&v.view_offset.z,e))return false;
        v.view_height=v.view_offset.z; *camera=v; *has_view=true;
    }
    return true;
}
bool frontend_unified_render_client_presentation_read(const frontend_unified_render *r,qa_actor_id actor,
    qa_application_camera_view *camera,qa_hud_value vitals[2],bool *has_view,bool *has_vitals,qa_error *e)
{
    qa_actor_id player;uint32_t slot;
    return r && frontend_remote_unified_current(r->replica,e) && render_frame_current(r) &&
        frontend_remote_unified_player(r->replica,&player,&slot) && qa_actor_id_equal(actor,player) &&
        client_presentation_read(r,actor,camera,vitals,has_view,has_vitals,e);
}

static bool hud_read(void *context, const qa_hud_frame *frame, qa_hud_data *out, qa_error *error)
{
    frontend_unified_render *r=context;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r->replica);
    if (!r->busy || !d || frame->seat!=d->physical_seat)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified HUD changed its received physical seat");
    *out=(qa_hud_data){.vitals=r->vitals,.vital_count=frame->source_status_native?0:r->ammo_label?3:2,.source_vitals=true,
        .crosshair_visible=true,.crosshair_color={1,1,1,1}};
    qa_application_camera_view camera;bool has_view=false,has_vitals=false;
    if(!frontend_unified_render_client_presentation_read(r,frame->actor,&camera,out->source_values,
        &has_view,&has_vitals,error))return false;
    if(has_vitals && !frame->source_status_native) {out->vitals=out->source_values;out->vital_count=2;}
    return true;
}
static bool model_source_read(frontend_unified_render *r,qa_json_id id,unified_render_model *m,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(r->frame);
    m->source_client=qa_json_string_equal(j,field(j,id,"renderOwner"),"source-client");
    qa_json_id source=field(j,id,"renderSource");
    if (!m->source_client || source==QA_JSON_NONE) return true;
    qa_buffer instance={0};
    bool okay=word(r->frame,field(j,source,"provider"),&m->source_provider,e) && m->source_provider &&
        qa_json_string(j,field(j,source,"instance"),&instance,e) && instance.data &&
        strlen((const char *)instance.data)==instance.size;
    if (okay) { m->source_instance=(char *)instance.data; instance=(qa_buffer){0}; }
    qa_buffer_free(&instance); return okay;
}
static bool model_equipment_read(frontend_unified_render *r,qa_json_id id,unified_render_model *m,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(r->frame);
    qa_json_id equipment=field(j,id,"renderEquipment");
    if (equipment==QA_JSON_NONE) return true;
    qa_buffer instance={0}; bool view_weapon=false;
    bool okay=qa_json_bool(j,field(j,id,"viewWeapon"),&view_weapon,e) && view_weapon &&
        word(r->frame,field(j,equipment,"provider"),&m->equipment_provider,e) && m->equipment_provider &&
        qa_json_bool(j,field(j,equipment,"slot"),&m->equipment_slot,e) &&
        qa_json_string(j,field(j,equipment,"instance"),&instance,e) && instance.data &&
        strlen((const char *)instance.data)==instance.size;
    if (okay) { m->equipment=true; m->equipment_instance=(char *)instance.data; instance=(qa_buffer){0}; }
    qa_buffer_free(&instance); return okay;
}
static bool player_blend_read(frontend_unified_render *r,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(r->frame);
    qa_json_id view=field(j,field(j,qa_unified_document_root(r->frame),"player"),"view");
    qa_scene_vec4 *colors[2]={&r->blend,&r->damage_blend};
    bool *present[2]={&r->has_blend,&r->has_damage_blend};
    const char *names[2]={"blend","damageBlend"};
    for (size_t i=0;i<2;++i) {
        qa_json_id color=field(j,view,names[i]);
        *present[i]=color!=QA_JSON_NONE && qa_json_type(j,color)!=QA_JSON_NULL;
        if (*present[i] && !(real(r->frame,field(j,color,"x"),&colors[i]->x,e) &&
            real(r->frame,field(j,color,"y"),&colors[i]->y,e) && real(r->frame,field(j,color,"z"),&colors[i]->z,e) &&
            real(r->frame,field(j,color,"w"),&colors[i]->w,e))) return false;
    }
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
    bool okay=model_source_read(r,id,m,e) && model_equipment_read(r,id,m,e) &&
        qa_json_string(j,field(j,id,"content"),&content,e) && qa_json_string(j,field(j,id,"path"),&path,e) &&
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
    if (okay) okay=player_blend_read(r,e);
    if (okay) {
        qa_actor_id viewer;uint32_t slot;qa_application_camera_view camera;
        qa_hud_value vitals[2];bool has_view=false,has_vitals=false;
        okay=frontend_remote_unified_player(replica,&viewer,&slot) &&
            client_presentation_read(r,viewer,&camera,vitals,&has_view,&has_vitals,e);
    }
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
typedef struct unified_scene_context {
    frontend_unified_render *renderer;
    const frontend_unified_prediction_view *predicted;
    const frontend_unified_render_children *children;
    qa_actor_id player;
    bool predicting;
} unified_scene_context;
static bool unified_scene_current(void *context)
{
    unified_scene_context *c=context; frontend_unified_render *r=c->renderer;
    return r->busy && frontend_unified_media_current(r->media) &&
        frontend_remote_unified_current(r->replica,NULL) && render_frame_current(r);
}
static bool unified_scene_visuals(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    unified_scene_context *c=context; frontend_unified_render *r=c->renderer;
    const frontend_unified_render_children *children=c->children;
    const frontend_unified_prediction_view *predicted=c->predicted;
    qa_actor_id player=c->player; bool predicting=c->predicting,okay=true;
    if(frame!=&r->frontend->frame || !unified_scene_current(c))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified visuals lost their actual received frame");
    bool reflected=qa_scene_world_q1_mirror_scope(frontend_unified_media_world(r->media),world,frame);
    for (size_t i=0;okay && i<r->model_count;++i) {
        unified_render_model *m=r->models+i;
        float scale=m->input.family==QA_SCENE_Q2 && m->scale==0?1:m->scale;
        if (!m->visible || m->input.color.w<=0 || scale==0 ||
            (reflected && m->input.view_model) ||
            (qa_actor_id_equal(m->actor,player) && !m->input.view_model && !reflected)) continue;
        if (m->source_client && m->source_instance && children && children->source_model) {
            bool owned=false;
            if (!children->source_model(children->context,m->actor,m->source_provider,m->source_instance,&owned,e)) return false;
            if (!unified_scene_current(c)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified model lost its completed Source receipt");
            if (owned) continue;
        }
        if (m->input.view_model && m->equipment && m->equipment_instance && children && children->equipment_model) {
            bool owned=false;
            if (!children->equipment_model(children->context,m->actor,m->equipment_provider,
                m->equipment_instance,m->equipment_slot,&owned,e)) return false;
            if (!unified_scene_current(c))
                return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified view model lost its completed EQUIPMENT receipt");
            if (owned) continue;
        }
        qa_scene_model_input input=m->input; input.view=world->view;
        qa_vec3 position=m->origin,previous=m->has_previous_origin?m->previous_origin:position;
        if (predicting && qa_actor_id_equal(m->actor,player)) {
            position=qa_vec_add(position,predicted->origin_shift); previous=qa_vec_add(previous,predicted->origin_shift);
        }
        if (input.view_model && input.family==QA_SCENE_Q1) { position.z+=2; previous.z+=2; }
        qa_model_transform_identity(&input.transform); qa_vec3 axes[3]; frontend_camera_axes(m->angles,axes);
        input.transform.origin[0]=position.x;input.transform.origin[1]=position.y;input.transform.origin[2]=position.z;
        for (unsigned a=0;a<3;++a) { input.transform.axes[a][0]=axes[a].x; input.transform.axes[a][1]=axes[a].y;
            input.transform.axes[a][2]=axes[a].z; input.transform.scale[a]=scale; }
        input.previous_origin=previous; input.ambient=qa_v3(1,1,1); input.identity_light=world->identity_light;
        input.video_frame=frontend_material_movies_frontend_resolve; input.video_context=r->frontend;
        if (m->media.brush_world) okay=qa_scene_world_submit_model(m->media.brush_world,m->media.inline_model,
            &input.transform,world,m->actor.slot,input.color,&r->frontend->frame,e);
        else {
            okay=qa_scene_world_sample_light_input(frontend_unified_media_world(r->media),world,position,
                &input.ambient,&input.directed,&input.light_direction,e) &&
                frontend_legacy_model_input(frontend_unified_media_world(r->media),world,&input,e);
            if (okay && children && children->model)
                okay=children->model(children->context,m->actor,m->product->identity,m->path,&input,e);
            if (okay) okay=qa_scene_model_submit(m->media.scene,&input,&r->frontend->frame,e);
            if (okay && children && children->model_after)
                okay=children->model_after(children->context,m->actor,m->product->identity,m->path,&input,&r->frontend->frame,e);
        }
        if (okay && input.view_model && !reflected) {
            m->submitted=true; m->submitted_cycle=frame->sequence;
        }
    }
    if (okay && children && children->world_models)
        okay=children->world_models(children->context,world,frame,e);
    return okay && unified_scene_current(c);
}
static bool unified_scene_particles(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_unified_render_children *children=c->children;
    if (frame!=&c->renderer->frontend->frame || !unified_scene_current(c))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified effects lost their entered received frame");
    if (children && children->particles && !children->particles(children->context,world,frame,e)) return false;
    if (children) {
        if (world->view.mirror) {
            if (children->world && !children->reflected_world)
                return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified mirror requires its retained supplemental projection");
            if (children->reflected_world && !children->reflected_world(children->context,world,frame,e)) return false;
        } else if (children->world && !children->world(children->context,&world->view,world,frame,e)) return false;
    }
    return unified_scene_current(c);
}
static bool unified_scene_blend(void *context,const qa_scene_world_input *world,qa_scene_vec4 blend,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_unified_render_children *children=c->children;
    return (!children || !children->blend || children->blend(children->context,world,blend,&c->renderer->frontend->frame,e)) &&
        unified_scene_current(c);
}
static bool unified_scene_dlights(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_scene_vec4 *blend,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_unified_render_children *children=c->children;
    return frame==&c->renderer->frontend->frame && unified_scene_current(c) &&
        (!children || !children->dlights || children->dlights(children->context,world,frame,blend,e)) &&
        unified_scene_current(c);
}
static bool unified_scene_reflected_lights(void *context,qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_unified_render_children *children=c->children;
    qa_scene_world *actual=frontend_unified_media_world(c->renderer->media);
    if(frame!=&c->renderer->frontend->frame || !unified_scene_current(c) ||
        !qa_scene_world_q1_mirror_scope(actual,world,frame))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified reflected lights lost their actual captured mirror");
    return (!children || !children->reflected_lights || children->reflected_lights(children->context,world,frame,e)) &&
        unified_scene_current(c) && qa_scene_world_q1_mirror_scope(actual,world,frame);
}
static bool unified_scene_policy(void *context,const qa_product *product,frontend_legacy_render_policy *out,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(c->renderer->replica);
    return domain && unified_scene_current(c) &&
        frontend_legacy_render_policy_read_registry(domain->cvars,product,out,e) && unified_scene_current(c);
}
static bool unified_sky_environment(frontend_unified_render *r,const qa_scene_world_input *world,
    qa_scene_q1_sky_environment *out,qa_error *e)
{
    const qa_cvars *registry=qa_application_cvars(r->frontend->application);
    const qa_cvar_view *fast=qa_cvars_find(registry,"r_fastsky"),*quality=qa_cvars_find(registry,"r_sky_quality"),
        *alpha=qa_cvars_find(registry,"r_skyalpha"),*fog=qa_cvars_find(registry,"r_skyfog"),
        *far_clip=qa_cvars_find(registry,"gl_farclip");
    if (!fast || !quality || !alpha || !fog || !far_clip || !isfinite(fast->number) ||
        !isfinite(quality->number) || !isfinite(alpha->number) || !isfinite(fog->number) ||
        !isfinite(far_clip->number) || far_clip->number<=4)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q1 sky lost its actual canonical controls");
    *out=(qa_scene_q1_sky_environment){.boxed=world->override_sky,.fast=fast->number!=0,
        .quality=fmaxf(1,truncf(quality->number)),.alpha=fminf(1,fmaxf(0,alpha->number)),
        .fog=fog->number,.far_clip=far_clip->number};
    if (out->boxed) memcpy(out->images,world->sky_images,sizeof(out->images));
    return true;
}
bool frontend_unified_render_draw(frontend_unified_render *r,const frontend_unified_prediction_view *predicted,
    const frontend_unified_render_children *children,float stereo,qa_audio_listener *listener,qa_error *e)
{
    if (!r || r->busy || !listener || !isfinite(stereo) || !frontend_unified_media_current(r->media))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified draw lost its actual received frame");
    for(size_t i=0;i<r->model_count;++i) { r->models[i].submitted=false; r->models[i].submitted_cycle=0; }
    if (!frontend_unified_material_movies_frame(r->media,&r->frontend->frame,e)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r->replica);
    qa_actor_id player; uint32_t source;
    if (!d || !frontend_remote_unified_player(r->replica,&player,&source)) return false;
    double fov=r->field_of_view; bool override;
    if (!r->explicit_fov && !frontend_view_settings_read(r->frontend->view_settings,&fov,&override))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified camera lost its real published FOV preference");
    qa_scene_view view={.viewport=frontend_viewport(r->frontend,d->physical_seat),.origin=r->origin,
        .clear_depth=true,.depth=1,.seat=d->physical_seat};
    qa_vec3 angles=r->angles; float height=r->height;
    qa_application_camera_view declared;qa_hud_value vitals[2];bool has_view=false,has_vitals=false;
    if(!frontend_unified_render_client_presentation_read(r,player,&declared,vitals,&has_view,&has_vitals,e))return false;
    bool predicting=predicted && (predicted->status==FRONTEND_UNIFIED_PREDICTION_ACTIVE ||
        predicted->status==FRONTEND_UNIFIED_PREDICTION_DISABLED);
    if (has_view) {view.origin=declared.origin;angles=declared.angles;height=declared.view_offset.z;}
    if (predicting && !has_view) {
        if (!qa_actor_id_equal(predicted->actor,player)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified predicted camera changed player");
        view.origin=qa_vec_add(view.origin,predicted->origin_shift); angles=predicted->view_angles;
        if (!r->source_view_offset) height=predicted->view_height;
    }
    if (!(fov>0 && fov<180))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified received camera has no finite field of view");
    if (children && children->view_origin &&
        !children->view_origin(children->context,player,view.origin,(float)fov,e)) return false;
    view.origin.z+=height;
    frontend_camera_axes(angles,view.axis); qa_vec3 local[3],basis[3]; memcpy(basis,view.axis,sizeof(basis));
    frontend_camera_axes(has_view?qa_v3(0,0,0):r->kick,local);
    for (unsigned i=0;i<3;++i) view.axis[i]=qa_vec_add(qa_vec_add(qa_vec_scale(basis[0],local[i].x),
        qa_vec_scale(basis[1],local[i].y)),qa_vec_scale(basis[2],local[i].z));
    if (!(fov>0 && fov<180) || !view.viewport.width || !view.viewport.height)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified received camera has no finite projection");
    float vertical=2*atanf(tanf((float)fov*.008726646259971648f)*(float)view.viewport.height/(float)view.viewport.width)*57.29577951308232f;
    view.projection=qa_scene_projection((float)fov,vertical,4,16384);
    if (children && children->camera) {
        float source_fov=(float)fov; bool owned=false;
        if (!children->camera(children->context,&view,&source_fov,&owned,e)) return false;
        if (owned) fov=source_fov;
        if (!(fov>0 && fov<180) || view.seat!=d->physical_seat ||
            !view.viewport.width || !view.viewport.height || !qa_vec_finite(view.origin) ||
            !qa_vec_finite(view.axis[0]) || !qa_vec_finite(view.axis[1]) || !qa_vec_finite(view.axis[2]))
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified compiled camera lost its actual physical view");
    }
    view.origin=qa_vec_add(view.origin,qa_vec_scale(view.axis[1],stereo));
    r->busy=true;
    qa_scene_world_input world={.view=view,.seconds=r->seconds,.milliseconds=(int64_t)(r->seconds*1000),.identity_light=1};
    world.video_frame=frontend_material_movies_frontend_resolve; world.video_context=r->frontend;
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
    if (okay && children && children->world_input)
        okay=children->world_input(children->context,&world,e);
    qa_executable_recipe *recipe=frontend_unified_media_recipe(r->media);
    const qa_recipe_choices *choices=qa_executable_recipe_choices(recipe);
    const qa_product *product=choices ? qa_catalog_product(qa_executable_recipe_catalog(recipe),choices->world.presentation) : NULL;
    qa_scene_q1_sky_environment sky;
    if (okay && !product) okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified world lost its selected presentation product");
    if (okay && product->family==QA_GAME_Q1) {
        okay=unified_sky_environment(r,&world,&sky,e);
        if (okay) {
            world.q1_sky_environment=&sky;
            float depth=sky.far_clip-4;
            world.view.projection.m[10]=-(sky.far_clip+4)/depth;
            world.view.projection.m[14]=-2*sky.far_clip*4/depth;
        }
    }
    if (okay) view=world.view;
    if (okay && children && children->lights)
        okay=children->lights(children->context,&view,&world,&world.lights,&world.light_count,e);
    unified_scene_context context={.renderer=r,.predicted=predicted,.children=children,.player=player,.predicting=predicting};
    frontend_legacy_scene_services services={.context=&context,.current=unified_scene_current,
        .visuals=unified_scene_visuals,.particles=unified_scene_particles,.dlights=unified_scene_dlights,
        .reflected_lights=unified_scene_reflected_lights,.blend=unified_scene_blend,.policy=unified_scene_policy};
    if (okay) okay=frontend_legacy_scene_submit_product(r->frontend,frontend_unified_media_world(r->media),
        product,&world,&r->frontend->frame,&services,e);
    if (okay && children && children->player_blend)
        okay=children->player_blend(children->context,player,r->has_blend,&r->blend,
            r->has_damage_blend,&r->damage_blend,view.viewport,&r->frontend->frame,e) &&
            unified_scene_current(&context);
    bool source_status=false;
    if(okay && children && children->status_replacement)
        okay=children->status_replacement(children->context,&source_status,e);
    if (okay) okay=qa_hud_draw(r->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,
        .time_ns=(uint64_t)(r->seconds*1e9),.viewport=view.viewport,.safe_area=view.viewport,.scale=1,.visible=true,
        .source_status_native=source_status},&r->frontend->frame,e);
    if (okay && children && children->hud)
        okay=children->hud(children->context,r->frontend->seats[d->physical_seat].ui,view.viewport,&r->frontend->frame,e);
    if (okay) { *listener=(qa_audio_listener){.seat=d->physical_seat,.actor=player.slot,.origin=view.origin,.gain=1};
        memcpy(listener->axis,view.axis,sizeof(view.axis)); }
    if (!okay)
        for(size_t i=0;i<r->model_count;++i) { r->models[i].submitted=false; r->models[i].submitted_cycle=0; }
    r->busy=false; return okay;
}
bool frontend_unified_render_idle(const frontend_unified_render *r)
{ return !r || (!r->busy && (!r->hud || qa_hud_idle(r->hud))); }
bool frontend_unified_render_destroy(frontend_unified_render **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_render *r=*slot;
    if (r->busy || (r->hud && !qa_hud_idle(r->hud)))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified received render frame still has callbacks");
    if (r->hud && !qa_hud_destroy(r->hud,e)) return false;
    for (size_t i=0;i<r->model_count;++i) {
        free(r->models[i].path); free(r->models[i].source_instance); free(r->models[i].equipment_instance);
    }
    free(r->models); free(r->ammo_label); qa_buffer_free(&r->area_bits);
    qa_unified_document_destroy(r->frame); free(r); *slot=NULL; return true;
}

static bool render_blob(qa_source_save_io *io,qa_buffer *value)
{
    size_t size=value->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        value->data=size?malloc(size):NULL; value->size=size;
        if (size && !value->data) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining received render continuation bytes");
    }
    return qa_source_save_bytes(io,value->data,size);
}
static bool render_model_fields(frontend_unified_render *r,unified_render_model *m,
    const frontend_unified_render_refs *refs,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_saved_actor_id actor={0}; uint64_t binding=0,material=0; uint32_t inline_model=m->media.inline_model;
    bool is_inline=m->media.is_inline; char *content=NULL;
    if (!reading) {
        if (!frontend_remote_unified_wire_actor(r->replica,m->actor,&actor)) return false;
        content=(char *)m->product->identity;
        for (size_t i=0;i<frontend_unified_media_model_count(r->media);++i) {
            frontend_unified_model_view row;
            if (frontend_unified_media_model_read(r->media,i,&row) &&
                row.scene==m->media.scene && row.world==m->media.brush_world && !is_inline) { binding=i+1; break; }
        }
        if ((!is_inline && !binding) ||
            (m->input.custom_material && !frontend_scene_material_encode(refs->scene,m->input.custom_material,&material,io->error))) return false;
    }
    bool okay=qa_source_save_u32(io,&actor.slot) && qa_source_save_u64(io,&actor.generation) &&
        qa_source_save_u64(io,&binding) && qa_source_save_bool(io,&is_inline) &&
        qa_source_save_u32(io,&inline_model) && frontend_save_text(io,&content) && frontend_save_text(io,&m->path) &&
        qa_source_save_vec3(io,&m->origin) && qa_source_save_vec3(io,&m->angles) &&
        qa_source_save_vec3(io,&m->previous_origin) && qa_source_save_f32(io,&m->scale) &&
        qa_source_save_bool(io,&m->visible) && qa_source_save_bool(io,&m->has_previous_origin) &&
        qa_source_save_u32(io,&m->input.frame) && qa_source_save_u32(io,&m->input.old_frame) &&
        qa_source_save_u32(io,&m->input.skin) && qa_source_save_u32(io,&m->input.flags) &&
        qa_source_save_f32(io,&m->input.back_lerp) && qa_source_save_f32(io,&m->input.color.x) &&
        qa_source_save_f32(io,&m->input.color.y) && qa_source_save_f32(io,&m->input.color.z) &&
        qa_source_save_f32(io,&m->input.color.w) && qa_source_save_bool(io,&m->input.view_model) &&
        qa_source_save_f64(io,&m->input.seconds) && qa_source_save_i64(io,&m->input.milliseconds) &&
        qa_source_save_bool(io,&m->input.has_milliseconds) && qa_source_save_u64(io,&material);
    if (okay && reading) {
        frontend_unified_bank_view bank={0}; bool found=false;
        for (size_t i=0;i<frontend_unified_media_bank_count(r->media);++i)
            if (frontend_unified_media_bank_read(r->media,i,&bank) && content && !strcmp(content,bank.content)) { found=true; break; }
        okay=found && m->path && *m->path &&
            frontend_remote_unified_actor_retained(r->replica,actor.slot,actor.generation,&m->actor,io->error);
        if (okay && is_inline) {
            okay=!binding && content && inline_model<qa_collision_model_count(frontend_remote_unified_geometry(r->replica)) &&
                qa_executable_recipe_choices(frontend_unified_media_recipe(r->media))->world.geometry==bank.product->id;
            m->media=(frontend_unified_model){.brush_world=frontend_unified_media_world(r->media),.inline_model=inline_model,.is_inline=true};
            m->input.family=bank.product->family==QA_GAME_Q1?QA_SCENE_Q1:bank.product->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3;
        } else if (okay) {
            frontend_unified_model_view row;
            okay=binding && binding<=frontend_unified_media_model_count(r->media) &&
                frontend_unified_media_model_read(r->media,(size_t)binding-1,&row) &&
                !strcmp(row.path,m->path) && row.bank<frontend_unified_media_bank_count(r->media);
            frontend_unified_bank_view actual;
            if (okay) okay=frontend_unified_media_bank_read(r->media,row.bank,&actual) && !strcmp(actual.content,content);
            if (okay) { m->media=(frontend_unified_model){.resource=row.resource,.opening=row.opening,.model=row.model,.scene=row.scene,.brush_world=row.world}; m->input.family=row.family; }
        }
        if (okay && material) okay=frontend_scene_material_decode(refs->scene,material,&m->input.custom_material,io->error);
        if (okay) { m->product=bank.product; m->input.material_library=bank.materials;
            m->input.entity=m->actor.slot; m->input.source_path=m->path; }
        if (okay) okay=isfinite(m->scale) && qa_vec_finite(m->origin) && qa_vec_finite(m->angles) &&
            qa_vec_finite(m->previous_origin) && isfinite(m->input.back_lerp) &&
            isfinite(m->input.color.x) && isfinite(m->input.color.y) && isfinite(m->input.color.z) &&
            isfinite(m->input.color.w) && isfinite(m->input.seconds);
    }
    if (reading) free(content);
    return okay;
}
static bool render_fields(frontend_unified_render *r,const frontend_unified_render_refs *refs,
    qa_source_save_io *io,qa_buffer *hud)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_buffer document={0};
    if (!reading) {
        qa_bytes bytes=qa_json_source(qa_unified_document_json(r->frame),qa_unified_document_root(r->frame));
        document=(qa_buffer){.data=(unsigned char *)bytes.data,.size=bytes.size};
    }
    bool okay=render_blob(io,&document);
    if (okay && reading) okay=qa_unified_document_create(QA_UNIFIED_FRAME_DOCUMENT,
        (qa_bytes){document.data,document.size},&r->frame,io->error);
    if (reading) qa_buffer_free(&document);
    okay=okay && render_blob(io,&r->area_bits) && qa_source_save_vec3(io,&r->origin) &&
        qa_source_save_vec3(io,&r->angles) && qa_source_save_vec3(io,&r->kick) &&
        qa_source_save_f32(io,&r->height) && qa_source_save_f64(io,&r->seconds) &&
        qa_source_save_f64(io,&r->field_of_view) && qa_source_save_bool(io,&r->explicit_fov) &&
        qa_source_save_bool(io,&r->source_view_offset) && frontend_save_text(io,&r->ammo_label);
    for (size_t i=0;okay && i<3;++i) okay=qa_source_save_f64(io,&r->vitals[i].value) && qa_source_save_bool(io,&r->vitals[i].warning);
    size_t count=r->model_count;
    if (okay) okay=qa_source_save_count(io,&count,reading?io->input.size-io->offset:SIZE_MAX);
    if (okay && reading) {
        if (count>SIZE_MAX/sizeof(*r->models)) return false;
        r->models=count?calloc(count,sizeof(*r->models)):NULL;
        okay=!count || r->models;
    }
    for (size_t i=0;okay && i<count;++i) { if (reading) r->model_count=i+1; okay=render_model_fields(r,r->models+i,refs,io); }
    if (okay && reading) {
        const qa_json_document *j=qa_unified_document_json(r->frame);
        qa_json_id rows=qa_json_get(j,qa_unified_document_root(r->frame),"models");
        okay=player_blend_read(r,io->error) && count==qa_json_size(j,rows) && qa_vec_finite(r->origin) && qa_vec_finite(r->angles) &&
            qa_vec_finite(r->kick) && isfinite(r->height) && isfinite(r->seconds) && r->seconds>=0 &&
            r->seconds*1e9<18446744073709551616.0 && isfinite(r->field_of_view) &&
            (!r->explicit_fov || (r->field_of_view>0 && r->field_of_view<180));
        for (size_t i=0;okay && i<3;++i) okay=isfinite(r->vitals[i].value);
        for (size_t i=0;okay && i<count;++i) {
            unified_render_model *m=r->models+i; qa_saved_actor_id wire;
            qa_json_id row=qa_json_at(j,rows,i),actor_id=qa_json_get(j,row,"actor");
            uint64_t slot,generation;
            bool view_model=false;
            okay=frontend_remote_unified_wire_actor(r->replica,m->actor,&wire) &&
                qa_json_u64(j,qa_json_get(j,actor_id,"slot"),&slot,io->error) && slot==wire.slot &&
                qa_json_u64(j,qa_json_get(j,actor_id,"generation"),&generation,io->error) && generation==wire.generation &&
                qa_json_string_equal(j,qa_json_get(j,row,"content"),m->product->identity) &&
                qa_json_string_equal(j,qa_json_get(j,row,"path"),m->path) &&
                model_source_read(r,row,m,io->error) && model_equipment_read(r,row,m,io->error) &&
                qa_json_bool(j,qa_json_get(j,row,"viewWeapon"),&view_model,io->error) && view_model==m->input.view_model;
        }
    }
    return okay && render_blob(io,hud);
}
static bool render_document_current(const frontend_unified_render *r,const qa_unified_document *published)
{
    if (!r || !published || !r->frame) return false;
    qa_bytes actual=qa_json_source(qa_unified_document_json(published),qa_unified_document_root(published));
    qa_bytes saved=qa_json_source(qa_unified_document_json(r->frame),qa_unified_document_root(r->frame));
    return actual.size==saved.size && (!actual.size || !memcmp(actual.data,saved.data,actual.size));
}
static bool render_frame_current(const frontend_unified_render *r)
{ return r && render_document_current(r,frontend_remote_unified_frame(r->replica)); }
bool frontend_unified_render_pending_current(const frontend_unified_render *r)
{ return r && frontend_unified_render_idle(r) &&
    render_document_current(r,frontend_remote_unified_frame_prepared(r->replica)); }
bool frontend_unified_render_equipment_read(const frontend_unified_render *r,qa_actor_id actor,
    frontend_unified_render_equipment *out,bool *present,qa_error *error)
{
    qa_actor_id viewer; uint32_t source_entity;
    if (!r || !out || !present || !frontend_remote_unified_current(r->replica,error) ||
        !frontend_unified_media_current(r->media) || !render_frame_current(r) ||
        !frontend_remote_unified_player(r->replica,&viewer,&source_entity) || !qa_actor_id_equal(actor,viewer))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Equipment receipt has no current published renderer and full viewer");
    const unified_render_model *selected=NULL;
    for (size_t i=0;i<r->model_count;++i) {
        const unified_render_model *model=r->models+i;
        if (!model->equipment || !model->input.view_model || !qa_actor_id_equal(model->actor,actor)) continue;
        if (selected)
            return frontend_unified_fail(error,QA_ERROR_FORMAT,"Published viewer has multiple selected equipment model rows");
        selected=model;
    }
    if (!selected) { *present=false; return true; }
    bool registered=false;
    if (selected->media.is_inline) {
        registered=selected->media.brush_world==frontend_unified_media_world(r->media) &&
            selected->media.inline_model<qa_collision_model_count(frontend_remote_unified_geometry(r->replica));
    } else for (size_t i=0;i<frontend_unified_media_model_count(r->media);++i) {
        frontend_unified_model_view model; frontend_unified_bank_view bank;
        if (!frontend_unified_media_model_read(r->media,i,&model) ||
            !frontend_unified_media_bank_read(r->media,model.bank,&bank))
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Equipment registry lost an actual model binding");
        if (model.resource==selected->media.resource && model.opening==selected->media.opening &&
            model.model==selected->media.model && model.scene==selected->media.scene &&
            model.world==selected->media.brush_world && model.family==selected->input.family &&
            bank.product==selected->product && bank.materials==selected->input.material_library &&
            !strcmp(model.path,selected->path)) { registered=true; break; }
    }
    if (!registered)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Equipment receipt lost its registered immutable resource tuple");
    *out=(frontend_unified_render_equipment){.actor=actor,.provider=selected->equipment_provider,
        .instance=selected->equipment_instance,.content=selected->product->identity,.path=selected->path,
        .slot=selected->equipment_slot,.visible=selected->visible && selected->input.color.w>0 &&
            (selected->scale!=0 || selected->input.family==QA_SCENE_Q2),
        .binding=selected->media,.input=&selected->input,.source_frame=r->replica->frame_number,
        .scene_sequence=r->frontend->frame.sequence};
    *present=true; return true;
}
static bool render_checkpoint(frontend_unified_render *r,
    const frontend_unified_render_refs *refs,qa_buffer *out,bool pending,qa_error *error)
{
    if (!r || !refs || !refs->scene || !out || out->data || out->size || !frontend_unified_render_idle(r) ||
        !frontend_remote_unified_checkpoint_current(r->replica,error) ||
        !(pending?frontend_unified_render_pending_current(r):render_frame_current(r)))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Received render capture requires its returned owner and shared dictionaries");
    qa_buffer hud={0}; qa_source_save_io io={0};
    bool okay=qa_hud_checkpoint(r->hud,&refs->hud,&hud,error) && qa_source_save_writer(&io,NULL,error) &&
        render_fields(r,refs,&io,&hud) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&hud);
    if (!okay && (!error || error->code==QA_OK))
        frontend_unified_fail(error,QA_ERROR_FORMAT,"Received render continuation lost its actual dictionary binding");
    return okay;
}
static bool render_restore(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_media *media,const frontend_unified_render_refs *refs,qa_bytes bytes,
    frontend_unified_render **out,bool pending,qa_error *error)
{
    if (!f || !replica || !media || !refs || !refs->scene || !out || *out ||
        frontend_unified_media_importing(media) || !frontend_unified_media_current(media) ||
        !frontend_remote_unified_checkpoint_current(replica,error)) return false;
    frontend_unified_render *r=calloc(1,sizeof(*r));
    if (!r) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining detached received render owner");
    r->frontend=f; r->replica=replica; r->media=media;
    qa_source_save_io io={0}; qa_buffer hud={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && render_fields(r,refs,&io,&hud) &&
        qa_source_save_finish(&io,NULL) &&
        render_document_current(r,pending?frontend_remote_unified_frame_prepared(replica):frontend_remote_unified_frame(replica));
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(replica);
    if (okay) okay=d && d->physical_seat<f->options.seats &&
        qa_hud_restore((qa_bytes){hud.data,hud.size},&(qa_hud_options){.ui=f->seats[d->physical_seat].ui,
            .application=d->application,.seat=d->physical_seat,.context=r,.read=hud_read},&refs->hud,&r->hud,error);
    r->vitals[0].label="Health"; r->vitals[1].label="Armor"; r->vitals[2].label=r->ammo_label;
    qa_source_save_dispose(&io); qa_buffer_free(&hud);
    if (!okay) {
        (void)frontend_unified_render_destroy(&r,NULL);
        if (!error || error->code==QA_OK)
            frontend_unified_fail(error,QA_ERROR_FORMAT,"Received render continuation has an invalid field or dictionary binding");
        return false;
    }
    *out=r; return true;
}

bool frontend_unified_render_checkpoint(frontend_unified_render *r,
    const frontend_unified_render_refs *refs,qa_buffer *out,qa_error *error)
{ return render_checkpoint(r,refs,out,false,error); }
bool frontend_unified_render_pending_checkpoint(frontend_unified_render *r,
    const frontend_unified_render_refs *refs,qa_buffer *out,qa_error *error)
{ return render_checkpoint(r,refs,out,true,error); }
bool frontend_unified_render_restore(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_media *media,const frontend_unified_render_refs *refs,qa_bytes bytes,
    frontend_unified_render **out,qa_error *error)
{ return render_restore(f,replica,media,refs,bytes,out,false,error); }
bool frontend_unified_render_pending_restore(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_media *media,const frontend_unified_render_refs *refs,qa_bytes bytes,
    frontend_unified_render **out,qa_error *error)
{ return render_restore(f,replica,media,refs,bytes,out,true,error); }
