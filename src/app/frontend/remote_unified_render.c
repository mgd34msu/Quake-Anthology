#include "remote_unified_private.h"
#include "remote_unified_metadata.h"
#include "remote_unified_render.h"
#include "view_settings.h"
#include "legacy_render_policy.h"
#include "remote_unified_material_movies_bridge.h"
#include "material_movies.h"
#include "equipment_media.h"
#include "menu_fonts.h"
#include "qa/game_q2.h"
#include "qa/game_q1.h"
#include "qa/application_ui_names.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"
#include "qa/scene_world_save.h"
#include "qa/unified_frame_visuals.h"
#include "qa/unified_frame_components.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct unified_render_model {
    const qa_unified_model_state *source;
    frontend_unified_model media;
    qa_scene_model_input input;
    const qa_product *product;
    const char *path;
    qa_actor_id actor;
    qa_vec3 origin, angles;
    qa_vec3 previous_origin;
    float scale;
    bool visible, has_previous_origin,native_held_weapon;
    bool source_client,submitted,flat_beam;
    uint32_t source_provider;
    const char *source_instance;
    uint64_t submitted_cycle, effects;
    uint32_t q1_effects;
    bool equipment,equipment_slot;
    uint32_t equipment_provider;
    const char *equipment_instance;
} unified_render_model;
struct frontend_unified_render {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    qa_unified_document *frame;
    qa_bytes area_bits;
    qa_unified_frame_lease *storage;
    unified_render_model *models;
    size_t model_count;
    qa_hud *hud;
    qa_hud_value vitals[3];
    qa_hud_team_face team_face;
    qa_hud_q1_status q1;
    qa_ui_preferences preferences;
    const char *ammo_label;
    qa_vec3 origin, angles, kick;
    qa_vec4 blend,damage_blend;
    float height;
    double seconds, field_of_view;
    int64_t milliseconds;
    bool explicit_fov, source_view_offset, busy;
    bool has_blend,has_damage_blend;
};
static bool render_frame_current(const frontend_unified_render *);
static bool presentation_source(const frontend_unified_render *r,const qa_unified_source_identity *source,qa_error *e)
{
    const qa_executable_recipe *recipe=frontend_remote_unified_recipe(r->replica);
    for(size_t i=0;i<qa_executable_recipe_provider_count(recipe);++i) {
        const qa_recipe_provider *row=qa_executable_recipe_provider(recipe,i);
        if(row->source_owner==source->provider && row->selection.runtime==QA_PROGRAM_QUAKEC && row->declaration &&
            !strcmp(source->instance,row->selection.instance))return true;
    }
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared QC output lacks its admitted Source identity");
}
static bool client_presentation_read(const frontend_unified_render *r,qa_actor_id actor,
    qa_application_camera_view *camera,qa_hud_value vitals[2],bool *has_view,bool *has_vitals,qa_error *e)
{
    if(!r || !r->frame || !camera || !vitals || !has_view || !has_vitals)return false;
    *has_view=*has_vitals=false;
    const qa_unified_frame *frame=qa_unified_document_frame(r->frame);
    if (!frame || !frame->player) return false;
    const qa_unified_client_presentation *value=frame->player->client_presentation;
    if (!value) return true;
    qa_actor_id actual;
    if(!frontend_remote_unified_source_actor(r->replica,frame,value->recipient,false,&actual,e) ||
        !qa_actor_id_equal(actual,actor))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared QC output changed its full received player");
    if(value->has_hud) {
        if(!presentation_source(r,&value->hud_source,e))return false;
        vitals[0]=(qa_hud_value){.label="Health",.value=value->health,.warning=value->health<=25};
        vitals[1]=(qa_hud_value){.label="Armor",.value=value->armor}; *has_vitals=true;
    }
    if(value->has_view) {
        if(!presentation_source(r,&value->view_source,e))return false;
        *camera=(qa_application_camera_view){.actor=actor,.cutscene=true,.origin=value->origin,
            .angles=value->angles,.view_height=value->view_height,.view_offset={0,0,value->view_height}};
        *has_view=true;
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
        .health_team_face=r->team_face,.q1=r->q1,.crosshair_visible=r->preferences.crosshair,
        .crosshair_size=r->preferences.crosshair_size,.crosshair_color={1,1,1,1}};
    qa_application_camera_view camera;bool has_view=false,has_vitals=false;
    if(!frontend_unified_render_client_presentation_read(r,frame->actor,&camera,out->source_values,
        &has_view,&has_vitals,error))return false;
    if(has_vitals && !frame->source_status_native) {
        out->vitals=out->source_values;out->vital_count=2;
        out->q1.health=qa_source_float_to_i32((float)out->source_values[0].value);
        out->q1.armor=(uint32_t)qa_source_float_to_i32((float)out->source_values[1].value);
    }
    if (frame->source_status_native) out->q1.present=false;
    if (out->q1.present) { out->vital_count=0; out->crosshair_visible=out->crosshair_visible && out->q1.health>0; }
    return true;
}
static bool player_blend_read(frontend_unified_render *r,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(r->frame);
    if (!frame || !frame->player) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified render lost its typed player view");
    const qa_unified_player_view *view=&frame->player->view;
    r->has_blend=view->has_blend; r->has_damage_blend=view->has_damage_blend;
    r->blend=(qa_vec4){view->blend[0],view->blend[1],view->blend[2],view->blend[3]};
    r->damage_blend=(qa_vec4){view->damage_blend[0],view->damage_blend[1],view->damage_blend[2],view->damage_blend[3]};
    if (frame->components) for (size_t i=0;i<frame->components->native_count;++i) {
        const qa_unified_native_camera *native=frame->components->native[i].view;
        if (!native) continue;
        r->has_blend=true; r->has_damage_blend=native->rerelease;
        r->blend=(qa_vec4){(float)native->blend[0],(float)native->blend[1],(float)native->blend[2],(float)native->blend[3]};
        r->damage_blend=(qa_vec4){(float)native->damage_blend[0],(float)native->damage_blend[1],(float)native->damage_blend[2],(float)native->damage_blend[3]};
    }
    return true;
}
static bool model_beam_read(unified_render_model *m, qa_error *e)
{
    m->input.model_beam = m->input.family == QA_GAME_Q2 &&
        qa_q2_model_beam(m->product->edition == QA_EDITION_RERELEASE ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
            m->input.flags, m->media.scene != NULL);
    m->input.beam_segment_length = (float)m->input.frame;
    if (m->flat_beam && m->input.frame > INT32_MAX)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 entity beam exceeds its Source width");
    return !(m->input.model_beam || m->flat_beam) || m->has_previous_origin ||
        frontend_unified_fail(e, QA_ERROR_FORMAT, "Q2 beam lost its actual Source endpoint");
}
static bool model_read(frontend_unified_render *r,const qa_unified_model_state *source,
    unified_render_model *m,qa_error *e)
{
    m->source=source;
    m->flat_beam=source->family==QA_GAME_Q2 && (source->visual.render_flags&128u) && source->path && !*source->path;
    qa_game_family kind=source->family==QA_GAME_Q1?QA_GAME_Q1:source->family==QA_GAME_Q2?QA_GAME_Q2:QA_GAME_Q3;
    m->source_client=source->render_source!=NULL;
    if(m->source_client) {
        m->source_provider=source->render_source->provider;
        m->source_instance=source->render_source->instance;
    }
    if(source->render_equipment) {
        m->equipment=true;m->equipment_slot=source->equipment_slot;
        m->equipment_provider=source->render_equipment->provider;
        m->equipment_instance=source->render_equipment->instance;
    }
    bool okay=frontend_remote_unified_source_actor(r->replica,qa_unified_document_frame(r->frame),source->actor,false,&m->actor,e);
    m->origin=source->origin; m->angles=source->angles; m->scale=source->visual.scale; m->visible=source->visual.visible; m->effects=source->visual.effects; m->q1_effects=source->q1_effects;
    m->input.frame=source->visual.frame<0?0:(uint32_t)source->visual.frame;
    m->input.old_frame=source->visual.old_frame<0?m->input.frame:(uint32_t)source->visual.old_frame;
    qa_scene_image_options images={.family=kind,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
    uint8_t translation[256];
    if (source->visual.has_player_colors) {
        unsigned top=source->visual.player_colors>>4,bottom=source->visual.player_colors&15;
        for (unsigned i=0;i<256;++i) translation[i]=(uint8_t)i;
        for (unsigned i=0;i<16;++i) { unsigned t=top*16,b=bottom*16;
            translation[16+i]=(uint8_t)(t<128?t+i:t+15-i);
            translation[96+i]=(uint8_t)(b<128?b+i:b+15-i); }
        images.translation=(qa_bytes){translation,sizeof(translation)};
    }
    qa_scene_resources *bank; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *sounds;
    qa_vfs *files;
    if (okay) okay=qa_executable_recipe_content(frontend_remote_unified_recipe(r->replica),source->content,&files,&m->product,e) &&
        frontend_unified_media_bank(r->media,source->content,&bank,&materials,&fonts,&sounds,e) &&
        (m->flat_beam || frontend_unified_media_model(r->media,source->content,source->path,kind,&images,&m->media,e));
    m->path=source->path;
    if (okay) {
        m->input.family=kind; m->input.skin=source->visual.skin<0 && !m->flat_beam?0:(uint32_t)source->visual.skin;
        m->input.flags=source->visual.render_flags;
        m->input.entity=m->actor.slot; m->input.material_library=frontend_unified_model_materials(m->media.scene); m->input.source_path=m->path;
        m->input.color=(qa_vec4){1,1,1,source->has_alpha?source->visual.alpha:1}; m->input.seconds=r->seconds;
        m->input.has_milliseconds=true; m->input.milliseconds=r->milliseconds;
        m->input.view_model=source->view_weapon; m->native_held_weapon=source->native_held_weapon; m->previous_origin=source->previous_origin;
        m->has_previous_origin=source->has_previous_origin; m->input.back_lerp=source->back_lerp;
        if (source->skin_path) okay=qa_material_register(materials,source->skin_path,&images,false,&m->input.custom_material,e);
        if (okay) okay=model_beam_read(m,e);
    }
    return okay;
}
static bool q1_team_face_prepare(frontend_unified_render *r,const qa_unified_player_ui *ui,qa_error *e)
{
    const qa_unified_q1_team_face *value=ui->q1_team_face;
    if (!value) return true;
    qa_vfs *files;const qa_product *product;
    if (!frontend_unified_media_files(r->media,value->content,&files,&product,e)) return false;
    if (product->family!=QA_GAME_Q1 || strcmp(product->campaign,"rogue") ||
        trunc(value->frags)!=value->frags || value->frags<INT32_MIN || value->frags>INT32_MAX)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Rogue team face differs from its received Source player");
    qa_scene_resources *images;qa_material_library *materials;qa_font_library *fonts;qa_audio_bank *sounds;
    return frontend_unified_media_bank(r->media,product->identity,&images,&materials,&fonts,&sounds,e) &&
        frontend_q1_hud_prepare(files,images,materials,QA_HUD_Q1_ROGUE,e) &&
        frontend_q1_team_face_read(images,materials,value->colors,(int32_t)value->frags,&r->team_face,e);
}
static bool q1_status_prepare(frontend_unified_render *r, const qa_unified_player_ui *ui,
    const qa_product *product, qa_error *e)
{
    if (!product || product->family != QA_GAME_Q1) return true;
    const qa_application_ui_names *names = qa_application_ui_names_read(r->frontend->application);
    const frontend_remote_unified_domain *domain = frontend_remote_unified_domain_read(r->replica);
    qa_vfs *files; const qa_product *admitted;
    qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *sounds;
    qa_hud_q1_variant variant = frontend_q1_hud_variant(product);
    if (!domain || !frontend_unified_media_files(r->media, product->identity, &files, &admitted, e) ||
        admitted != product || !frontend_unified_media_bank(r->media, product->identity,
            &images, &materials, &fonts, &sounds, e) ||
        !frontend_q1_hud_prepare(files, images, materials, variant, e)) return false;
    qa_q1_program program = variant == QA_HUD_Q1_ROGUE ? QA_Q1_ROGUE :
        variant == QA_HUD_Q1_HIPNOTIC ? QA_Q1_HIPNOTIC : QA_Q1_ID1;
    qa_q1_clientdata client = {.health = qa_source_float_to_i32((float)ui->health),
        .armor = (uint32_t)qa_source_float_to_i32((float)ui->armor.regular.points),
        .ammo = ui->has_ammo ? (uint32_t)qa_source_float_to_i32((float)ui->ammo_count) : 0};
    for (unsigned bit = 0; bit < 32; ++bit) {
        qa_q1_weapon weapon; qa_q1_weapon_profile profile;
        if (!qa_q1_weapon_source(program, UINT32_C(1) << bit, &weapon) ||
            !qa_q1_weapon_profile_identity(program, weapon, &profile)) continue;
        for (size_t i = 0; i < ui->item_count; ++i)
            if (ui->items[i].owned && ui->items[i].id == names->q1_weapons[weapon]) client.items |= UINT32_C(1) << bit;
        if (ui->active_weapon && ui->active_weapon == names->q1_weapons[weapon]) client.weapon = UINT32_C(1) << bit;
    }
    uint32_t *counts[] = {&client.shells, &client.nails, &client.rockets, &client.cells};
    for (unsigned i = 0; i < 7; ++i) {
        if (i < 4) for (size_t j = 0; j < ui->inventory_count; ++j)
            if (ui->inventory[j].item == names->q1_ammo[i]) *counts[i] =
                (uint32_t)qa_source_float_to_i32((float)ui->inventory[j].count);
        if (ui->ammo_item && ui->ammo_item == names->q1_ammo[i]) {
            unsigned bit = i < 4 ? (variant == QA_HUD_Q1_ROGUE ? 7u : 8u) + i :
                i == QA_Q1_LAVA_NAILS ? 26u : i == QA_Q1_MULTI_ROCKETS ? 28u : 27u;
            client.items |= UINT32_C(1) << bit;
        }
    }
    for (size_t j = 0; j < ui->inventory_count; ++j) for (unsigned i = 0; i < 2; ++i)
        if (ui->inventory[j].count > 0 && ui->inventory[j].item == names->q1_keys[i]) client.items |= 131072u << i;
    static const struct { qa_q1_power power; unsigned bit; } powers[] = {
        {QA_Q1_INVISIBILITY, 19}, {QA_Q1_INVULNERABILITY, 20}, {QA_Q1_SUIT, 21}, {QA_Q1_QUAD, 22},
        {QA_Q1_WETSUIT, 24}, {QA_Q1_EMPATHY, 25}, {QA_Q1_SHIELD, 29}, {QA_Q1_ANTIGRAV, 30}};
    for (size_t j = 0; j < ui->powerup_count; ++j) for (size_t i = 0; i < sizeof(powers) / sizeof(*powers); ++i)
        if (ui->powerups[j].seconds > 0 && ui->powerups[j].id == names->q1_powers[powers[i].power])
            client.items |= UINT32_C(1) << powers[i].bit;
    if (ui->armor.regular.kind == QA_ARMOR_Q1 && ui->armor.regular.points > 0) {
        unsigned grade = ui->armor.regular.protection.q1_absorption >= .8f ? 2u : ui->armor.regular.protection.q1_absorption >= .6f ? 1u : 0u;
        client.items |= UINT32_C(1) << ((variant == QA_HUD_Q1_ROGUE ? 23u : 13u) + grade);
    }
    const qa_scene_image *face = NULL;
    frontend_q1_view_motion motion = {0};
    if (!frontend_q1_face_read(materials, frontend_view_q1_face(client.health, client.items, r->seconds, &motion),
            &face, e) || !frontend_q1_hud_read(materials, &client, product, domain->cvars,
            &r->replica->legacy_cvars.hud, r->frontend->view_settings,
            product->edition == QA_EDITION_QUAKEWORLD, r->seconds, face, &r->q1, e)) return false;
    const qa_unified_frame_metadata *metadata = frontend_remote_unified_metadata(r->replica);
    if (metadata && metadata->q1) {
        r->q1.level = metadata->q1->level; r->q1.total_secrets = metadata->q1->total_secrets;
        r->q1.total_monsters = metadata->q1->total_monsters; r->q1.found_secrets = metadata->q1->found_secrets;
        r->q1.killed_monsters = metadata->q1->killed_monsters;
    }
    r->q1.reduced_flashes = r->preferences.reduced_flashes;
    return true;
}
static bool hud_presentation(void *context, qa_ui_presentation *out, qa_error *e)
{
    frontend_unified_render *r = context;
    const frontend_remote_unified_domain *domain = frontend_remote_unified_domain_read(r->replica);
    if (!domain || !frontend_menu_font_selection(r->frontend, domain->physical_seat,
        r->preferences.typeface == QA_UI_TYPEFACE_BOLD, &out->fonts, e)) return false;
    if (r->preferences.typeface != QA_UI_TYPEFACE_BOLD) out->fonts.primary = NULL;
    out->text_scale = r->preferences.text_scale; out->color_mode = r->preferences.color_mode;
    return true;
}
bool frontend_unified_render_create(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_media *media,qa_unified_frame_pool *pool,const qa_unified_document *frame,frontend_unified_render **out,qa_error *e)
{
    if (!f || !replica || !media || !frame || !out || *out ||
        qa_unified_document_type(frame)!=QA_UNIFIED_FRAME_DOCUMENT || !frontend_unified_media_current(media))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified frame preparation lost its actual replica media");
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(pool,e);
    if(!storage) return false;
    frontend_unified_render *r=qa_unified_frame_lease_alloc(storage,1,sizeof(*r),_Alignof(frontend_unified_render),e);
    if(!r) { qa_unified_frame_lease_release(storage);return false; }
    r->storage=storage;
    r->frontend=f; r->replica=replica; r->media=media;
    const qa_unified_frame *received=qa_unified_document_frame(frame);
    bool okay=received && received->world && received->player && received->visuals && qa_unified_document_retain(frame,&r->frame,e);
    if (!okay) { qa_unified_frame_lease_release(storage); return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified renderer requires its actual typed world/player/visual frame"); }
    r->seconds=received->world->presentation_seconds;
    r->milliseconds=qa_unified_world_frame_milliseconds(received->world);
    r->area_bits=(qa_bytes){received->world->area_bits.data,received->world->area_bits.size};
    const qa_unified_player_view *view=&received->player->view;
    const qa_unified_player_ui *ui=&received->player->ui;
    const frontend_remote_unified_domain *hud_domain=frontend_remote_unified_domain_read(replica);
    qa_application_client_source hud_source;
    if (okay) okay=hud_domain && qa_application_client_physical_read(hud_domain->application,
        (qa_actor_owner)hud_domain->command_context.owner,hud_domain->command_context.seat,&hud_source,e);
    const qa_product *hud_product=okay?qa_catalog_product(qa_launch_instance_catalog(hud_source.descriptor),
        hud_source.descriptor->selection.product):NULL;
    if (okay) okay=qa_ui_preferences_read(qa_application_cvars(f->application),qa_application_ui_preference_handles(f->application),
        hud_domain->physical_seat,&r->preferences,e) && q1_team_face_prepare(r,ui,e) && q1_status_prepare(r,ui,hud_product,e);
    r->origin=view->origin; r->angles=view->angles; r->height=view->view_height;
    r->source_view_offset=view->has_client_view_offset_delta; r->kick=view->kick_angles;
    r->explicit_fov=view->has_field_of_view; r->field_of_view=view->field_of_view;
    if (okay) okay=player_blend_read(r,e);
    if (okay) {
        qa_actor_id viewer;uint32_t slot;qa_application_camera_view camera;
        qa_hud_value vitals[2];bool has_view=false,has_vitals=false;
        okay=frontend_remote_unified_player(replica,&viewer,&slot) && client_presentation_read(r,viewer,&camera,vitals,&has_view,&has_vitals,e);
    }
    r->vitals[0]=(qa_hud_value){.label="Health",.value=ui->health,.warning=ui->health<=25};
    r->vitals[1]=(qa_hud_value){.label="Armor",.value=ui->armor.regular.points};
    if (okay && ui->has_ammo) {
        const char *label=ui->weapon_status?ui->weapon_status->label:qa_strings_cstr(replica->strings,ui->ammo_item);
        r->ammo_label=label;
        r->vitals[2]=(qa_hud_value){.label=r->ammo_label,.value=ui->ammo_count,
            .warning=ui->arsenal_warning==QA_AMMO_EMPTY || ui->arsenal_warning==QA_AMMO_LOW};
    }
    size_t count=received->visuals->model_count;
    if (okay && count) { r->models=qa_unified_frame_lease_alloc(storage,count,sizeof(*r->models),_Alignof(unified_render_model),e); okay=r->models!=NULL;
        if (!okay) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received unified model bindings"); }
    for (size_t i=0;okay && i<count;++i) { r->model_count=i+1; okay=model_read(r,received->visuals->models+i,r->models+i,e); }
    if (okay) r->hud=f->seats[hud_domain->physical_seat].hud;
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
        float scale=m->input.family==QA_GAME_Q2 && m->scale==0?1:m->scale;
        const qa_unified_frame *received=qa_unified_document_frame(r->frame);
        bool native_hidden=false;
        if (m->native_held_weapon && received->components)
            for (size_t k=0;k<received->components->native_count;++k)
                if (received->components->native[k].view && !received->components->native[k].view->weapon_visible) native_hidden=true;
        if (!m->visible || native_hidden || m->input.color.w<=0 || scale==0 ||
            (reflected && m->input.view_model) ||
            (qa_actor_id_equal(m->actor,player) && !m->input.view_model && !reflected)) continue;
        if (m->input.view_model && m->source->q3_weapon && children && children->selected_weapon) {
            bool submitted=false;
            if (!children->selected_weapon(children->context,m->source,world,frame,&submitted,e)) return false;
            m->submitted=submitted;m->submitted_cycle=frame->sequence;
            continue;
        }
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
        if (input.view_model && input.family==QA_GAME_Q1) { position.z+=2; previous.z+=2; }
        qa_vec3 angles = input.view_model ? m->angles : frontend_legacy_entity_angles(input.family,
            m->product->edition, m->media.model, m->effects, m->angles, world->seconds, world->milliseconds);
        qa_model_transform_identity(&input.transform); qa_vec3 axes[3]; frontend_camera_axes(angles,axes);
        input.transform.origin[0]=position.x;input.transform.origin[1]=position.y;input.transform.origin[2]=position.z;
        for (unsigned a=0;a<3;++a) { input.transform.axes[a][0]=axes[a].x; input.transform.axes[a][1]=axes[a].y;
            input.transform.axes[a][2]=axes[a].z; input.transform.scale[a]=scale; }
        input.previous_origin=previous; input.ambient=qa_v3(1,1,1); input.identity_light=world->identity_light;
        input.video_frame=frontend_material_movies_frontend_resolve; input.video_context=r->frontend;
        if (m->flat_beam) {
            if (!children || !children->entity_beam)
                return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 entity beam has no entered renderer");
            okay=children->entity_beam(children->context,m->product->identity,&world->view,
                position,previous,m->input.skin,(int32_t)m->input.frame,frame,e);
        }
        else if (m->media.brush_world) okay=qa_scene_world_submit_model(m->media.brush_world,m->media.inline_model,
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
static bool unified_scene_blend(void *context,const qa_scene_world_input *world,qa_vec4 blend,qa_error *e)
{
    unified_scene_context *c=context;
    const frontend_unified_render_children *children=c->children;
    return (!children || !children->blend || children->blend(children->context,world,blend,&c->renderer->frontend->frame,e)) &&
        unified_scene_current(c);
}
static bool unified_scene_dlights(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_vec4 *blend,qa_error *e)
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
        frontend_legacy_render_policy_read_controls(domain->cvars,&c->renderer->replica->legacy_cvars,product,out,e) && unified_scene_current(c);
}
static bool unified_sky_environment(frontend_unified_render *r,const qa_scene_world_input *world,
    qa_scene_q1_sky_environment *out,qa_error *e)
{
    const qa_cvars *registry=qa_application_cvars(r->frontend->application);
    if (!frontend_q1_sky_controls_read(registry,&r->replica->sky_controls,out,NULL) || out->far_clip<=4)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q1 sky lost its actual canonical controls");
    out->boxed=world->override_sky;
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
    bool source_status=false;
    if (children && children->status_replacement &&
        !children->status_replacement(children->context,&source_status,e)) return false;
    if (children && children->q1_status) children->q1_status(children->context,&r->q1);
    if (r->q1.present && !source_status) {
        qa_hud_q1_placement status=qa_hud_q1_place(view.viewport,r->preferences.hud_scale,r->q1.view_size,
            r->q1.overlay_status,r->q1.intermission,r->q1.deathmatch);
        uint32_t available=view.viewport.height-status.reserved;
        double fraction=fmin(r->q1.view_size,100)/100;
        uint32_t width=(uint32_t)fmax(96,trunc(view.viewport.width*fraction));
        if (width>view.viewport.width) width=view.viewport.width;
        uint32_t height=(uint32_t)fmax(1,trunc(view.viewport.height*fraction));
        if (height>available) height=available;
        view.viewport.x+=(int32_t)((view.viewport.width-width)/2);
        if (r->q1.view_size<100) view.viewport.y+=(int32_t)((available-height)/2);
        view.viewport.width=width;view.viewport.height=height;
    }
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
    const qa_unified_frame *received=qa_unified_document_frame(r->frame);
    const qa_unified_native_camera *native_camera=NULL;
    if (received->components) for (size_t i=0;i<received->components->native_count;++i)
        if (received->components->native[i].view) {
            if (native_camera) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified camera has multiple native Source views");
            native_camera=received->components->native[i].view;
        }
    qa_vec3 kick=has_view?qa_v3(0,0,0):r->kick;
    double native_origin_z=0;bool native_view=false;
    if (native_camera && !has_view) {
        const qa_unified_native_camera *source_view=native_camera;
        qa_vec3 selected_origin=predicting?qa_movement_origin(&predicted->state):received->player->view.origin;
        double origin[3]={source_view->origin[0],source_view->origin[1],source_view->origin[2]};
        if (source_view->position_prediction) {
            origin[0]=(double)selected_origin.x+origin[0]-source_view->movement_origin.x;
            origin[1]=(double)selected_origin.y+origin[1]-source_view->movement_origin.y;
            origin[2]=(double)selected_origin.z+origin[2]-source_view->movement_origin.z;
        }
        view.origin=qa_v3((float)origin[0],(float)origin[1],(float)origin[2]);
        native_origin_z=origin[2];native_view=true;
        if (!source_view->angular_prediction)
            angles=qa_v3((float)source_view->angles[0],(float)source_view->angles[1],(float)source_view->angles[2]);
        height=(float)source_view->view_height; fov=source_view->field_of_view;
        kick=qa_v3((float)source_view->kick_angles[0],(float)source_view->kick_angles[1],(float)source_view->kick_angles[2]);
    }
    if (!(fov>0 && fov<180))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified received camera has no finite field of view");
    if (children && children->view_origin &&
        !children->view_origin(children->context,player,view.origin,(float)fov,e)) return false;
    if (native_view && !native_camera->rerelease)view.origin.z=(float)(native_origin_z+native_camera->view_height);
    else view.origin.z+=height;
    frontend_camera_axes(angles,view.axis); qa_vec3 local[3],basis[3]; memcpy(basis,view.axis,sizeof(basis));
    frontend_camera_axes(kick,local);
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
    qa_scene_world_input world={.view=view,.seconds=r->seconds,.milliseconds=r->milliseconds,.identity_light=1,
        .no_world=native_camera && (native_camera->render_flags&1)!=0};
    frontend_unified_media_world_scratch(r->media,&world);
    world.video_frame=frontend_material_movies_frontend_resolve; world.video_context=r->frontend;
    world.visible_areas=r->area_bits.data; world.visible_area_bytes=r->area_bits.size;
    float q1[256]; qa_vec3 q2[256]; for (size_t i=0;i<256;++i) { q1[i]=256; q2[i]=qa_v3(1,1,1); }
    bool okay=received && received->world;
    const qa_unified_frame_metadata *metadata=frontend_remote_unified_metadata(r->replica);
    okay=okay && metadata && metadata->epoch==received->epoch && metadata->frame<=received->world->source.number;
    for (size_t i=0;okay && i<metadata->style_count;++i) {
        const qa_unified_style_pattern *style=metadata->styles+i;
        if (style->index>=256) {okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified lightstyle exceeds its Source table");break;}
        float value=frontend_legacy_lightstyle_sample(style->family,style->pattern,r->seconds);
        if (style->family==QA_GAME_Q1)q1[style->index]=value;
        else q2[style->index]=qa_v3(value,value,value);
    }
    world.q1_styles=q1; world.q2_styles=q2; world.style_count=256;
    for (size_t i=0;okay && children && children->entity_effects && i<r->model_count;++i) {
        const unified_render_model *model=r->models+i;
        if (!model->visible || model->input.view_model) continue;
        frontend_unified_render_entity_effects entity={.actor=model->actor,.product=model->product,
            .model=model->media.model,.family=model->input.family,.origin=model->origin,
            .angles=model->angles,.effects=model->effects,.q1_effects=model->q1_effects};
        okay=children->entity_effects(children->context,&entity,world.seconds,e);
    }
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
    qa_scene_rect output = frontend_viewport(r->frontend, d->physical_seat);
    if (okay) okay=frontend_view_background(r->frontend,output,&view,e);
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
    if (okay) okay=qa_hud_draw_content(r->hud,&(qa_hud_options){.ui=r->frontend->seats[d->physical_seat].ui,
        .application=d->application,.seat=d->physical_seat,.context=r,.read=hud_read,.presentation=hud_presentation},&(qa_hud_frame){.seat=d->physical_seat,.actor=player,
        .time_ns=r->seconds>0?(uint64_t)(r->seconds*1e9):0,.viewport=view.viewport,.safe_area=output,.scale=r->preferences.hud_scale,.visible=true,
        .source_status_native=source_status,.show_scores=qa_input_seat_action_active(r->frontend->seats[d->physical_seat].input,QA_INPUT_SCORES)},&r->frontend->frame,e);
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
    qa_unified_frame_lease *storage=r->storage;
    qa_unified_document_destroy(r->frame);
    qa_unified_frame_lease_release(storage);
    *slot=NULL;return true;
}

static bool render_document_current(const frontend_unified_render *r,const qa_unified_document *published)
{
    return r && r->frame && r->frame==published;
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
            bank.product==selected->product && bank.materials==(selected->media.scene?
                qa_scene_model_material_owner(selected->media.scene):qa_scene_world_material_owner(selected->media.brush_world)) &&
            !strcmp(model.path,selected->path)) { registered=true; break; }
    }
    if (!registered)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Equipment receipt lost its registered immutable resource tuple");
    *out=(frontend_unified_render_equipment){.actor=actor,.provider=selected->equipment_provider,
        .instance=selected->equipment_instance,.content=selected->product->identity,.path=selected->path,
        .slot=selected->equipment_slot,.visible=selected->visible && selected->input.color.w>0 &&
            (selected->scale!=0 || selected->input.family==QA_GAME_Q2),
        .binding=selected->media,.input=&selected->input,.source_frame=r->replica->frame_number,
        .scene_sequence=r->frontend->frame.sequence};
    *present=true; return true;
}
