#include "capture.h"
#include "visual_restore.h"
#include "native_q2_save.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "equipment_events.h"
#include "selected_character.h"
#include "selected_character_lifetime.h"
#include "selected_effects.h"
#include "save_commands.h"
#include "qc_rerelease_events.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/material_save.h"
#include "qa/font_save.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"

typedef enum capture_kind { CAPTURE_ASSETS, CAPTURE_IMAGES, CAPTURE_LIBRARY,
    CAPTURE_FONTS, CAPTURE_ORDER, CAPTURE_WORLD, CAPTURE_MODEL } capture_kind;
typedef struct capture_row {
    capture_kind kind;
    const void *owner;
    union {
        qa_scene_resources_capture *images;
        qa_font_library_capture *fonts;
        qa_scene_world_capture *world;
        qa_scene_model_capture *model;
    } token;
    bool held;
} capture_row;
struct frontend_capture {
    qa_frontend *frontend;
    capture_row *rows;
    size_t count, capacity;
};

static bool resources_idle(const qa_scene_resources *images)
{ return !images || qa_scene_resources_idle(images); }
static bool library_idle(const qa_material_library *library)
{ return !library || qa_material_library_idle(library); }
static bool fonts_idle(const qa_font_library *fonts)
{ return !fonts || qa_font_library_idle(fonts); }
bool frontend_seat_callbacks_returned(const qa_frontend *f)
{
    if (!f) return false;
    if (!f->seats) return true;
    for (unsigned i=0;i<f->options.seats;++i) {
        const frontend_seat *seat=f->seats+i;
        if ((seat->ui && !qa_ui_idle(seat->ui)) || (seat->hud && !qa_hud_idle(seat->hud)) ||
            (seat->wheel && !qa_hud_wheel_round_ready(seat->wheel))) return false;
    }
    return true;
}
bool frontend_seat_callbacks_idle(const qa_frontend *f)
{
    if (!frontend_seat_callbacks_returned(f)) return false;
    if (f->seats) for (unsigned i=0;i<f->options.seats;++i)
        if (!qa_input_release_idle(f->seats[i].input)) return false;
    return true;
}
bool frontend_owners_returned(const qa_frontend *f)
{
    if (!f || f->capture ||
        !frontend_cinematic_idle(f) || !frontend_qc_rerelease_idle(f) || !frontend_native_q2_children_idle(f) ||
        !frontend_native_q3_idle(f) || !frontend_remote_q3_idle(f) || !frontend_ui_features_idle(f) ||
        !frontend_equipment_idle(f) || !frontend_equipment_q3_idle(f) || !frontend_equipment_gear_idle(f) ||
        !frontend_equipment_events_idle(f->gear_events) ||
        !frontend_selected_character_idle(f) || !frontend_selected_effects_idle(f) || !frontend_sources_idle(f) ||
        !resources_idle(f->images) || !resources_idle(f->ui_images) || !library_idle(f->materials) ||
        !fonts_idle(f->fonts) || (f->order && !qa_material_order_idle(f->order)) ||
        (f->scene_world && !qa_scene_world_idle(f->scene_world)) || !frontend_visuals_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,NULL)) ||
        (f->device && !qa_audio_device_round_ready(f->device,NULL))) return false;
    for (size_t i=0;;++i) {
        const qa_scene_resources *images=frontend_event_images_at((qa_frontend *)f,i);
        if (!images) break;
        if (!qa_scene_resources_idle(images)) return false;
    }
    return true;
}
bool frontend_owners_idle(const qa_frontend *f)
{
    return frontend_owners_returned(f) && (!f->input || qa_input_platform_settings_idle(f->input));
}
static bool add(frontend_capture *capture, capture_kind kind, const void *owner, qa_error *error)
{
    if (!owner) return true;
    for (size_t i=0;i<capture->count;++i)
        if (capture->rows[i].kind==kind && capture->rows[i].owner==owner) return true;
    if (capture->count==capture->capacity) {
        size_t capacity=capture->capacity?capture->capacity*2:16;
        if (capacity<capture->capacity || capacity>SIZE_MAX/sizeof(*capture->rows))
            return frontend_fail(error,QA_ERROR_MEMORY,"Frontend capture owner inventory exceeds address space");
        capture_row *rows=realloc(capture->rows,capacity*sizeof(*rows));
        if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual frontend capture owners");
        capture->rows=rows; capture->capacity=capacity;
    }
    capture->rows[capture->count++]=(capture_row){.kind=kind,.owner=owner}; return true;
}
static bool hold(capture_row *row, qa_error *error)
{
    bool ok=false;
    switch (row->kind) {
    case CAPTURE_ASSETS: ok=qa_q3_assets_capture_begin((qa_q3_presentation_assets *)row->owner,error); break;
    case CAPTURE_IMAGES: ok=qa_scene_resources_capture_begin(row->owner,&row->token.images,error); break;
    case CAPTURE_LIBRARY: ok=qa_material_library_capture_begin(row->owner,error); break;
    case CAPTURE_FONTS: ok=qa_font_library_capture_begin(row->owner,&row->token.fonts,error); break;
    case CAPTURE_ORDER: ok=qa_material_order_capture_begin(row->owner,error); break;
    case CAPTURE_WORLD: ok=qa_scene_world_capture_begin(row->owner,&row->token.world,error); break;
    case CAPTURE_MODEL: ok=qa_scene_model_capture_begin(row->owner,&row->token.model,error); break;
    }
    row->held=ok; return ok;
}
static bool heaps(frontend_capture *capture, qa_error *error)
{
    qa_frontend *f=capture->frontend;
    if (!add(capture,CAPTURE_IMAGES,f->ui_images,error) || !add(capture,CAPTURE_IMAGES,f->images,error) ||
        !add(capture,CAPTURE_LIBRARY,f->materials,error) || !add(capture,CAPTURE_FONTS,f->fonts,error) ||
        !add(capture,CAPTURE_ORDER,f->order,error)) return false;
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f,i,&group))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture source is not fully constructed");
        if (!add(capture,CAPTURE_IMAGES,group.images,error) || !add(capture,CAPTURE_LIBRARY,group.materials,error) ||
            !add(capture,CAPTURE_FONTS,group.fonts,error) || !add(capture,CAPTURE_WORLD,group.world,error)) return false;
    }
    for (size_t i=0;;++i) {
        const qa_scene_resources *images=frontend_event_images_at(f,i);
        if (!images) break;
        if (!add(capture,CAPTURE_IMAGES,images,error)) return false;
    }
    for (size_t i=0;i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view owner;
        if (!frontend_visual_owner_read(f,i,&owner))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture appearance owner is incomplete");
        if (!add(capture,CAPTURE_IMAGES,owner.images,error) || !add(capture,CAPTURE_LIBRARY,owner.materials,error)) return false;
        for (size_t j=0;j<frontend_visual_model_count(f,i);++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(f,i,j,&model) || !add(capture,CAPTURE_MODEL,model.scene,error))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture appearance model is incomplete");
        }
    }
    for (size_t i=0;i<frontend_native_q2_owner_count(f);++i) {
        frontend_native_q2_owner_view owner;
        if (!frontend_native_q2_owner_read(f,i,&owner) || owner.prepared)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture native source is incomplete");
        if (!add(capture,CAPTURE_IMAGES,owner.images,error) || !add(capture,CAPTURE_FONTS,owner.fonts,error)) return false;
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        if (!frontend_native_q3_read(f,i,&owner,error) || !owner.assets || !owner.images ||
            !owner.materials || !owner.fonts || !owner.core)
            return error && error->code!=QA_OK?false:
                frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture native Q3 owner is incomplete");
        if (!add(capture,CAPTURE_IMAGES,owner.images,error) || !add(capture,CAPTURE_LIBRARY,owner.materials,error) ||
            !add(capture,CAPTURE_FONTS,owner.fonts,error)) return false;
    }
    for (size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view media;
        if (!frontend_equipment_media_at(f,i,&media) || !media.declaration || !media.held ||
            (!media.declaration->none && (!media.held_scene || !media.held->model)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture equipment media is incomplete");
        if (media.declaration->none) continue;
        if (!add(capture,CAPTURE_MODEL,media.held_scene,error)) return false;
    }
    return add(capture,CAPTURE_WORLD,f->scene_world,error);
}
static bool registry_children(frontend_capture *capture, qa_error *error)
{
    size_t registries=capture->count;
    for (size_t i=0;i<registries;++i) {
        if (capture->rows[i].kind!=CAPTURE_ASSETS) continue;
        const qa_q3_presentation_assets *assets=capture->rows[i].owner;
        qa_q3_presentation_asset_options services; qa_scene_world *world=NULL;
        qa_collision_geometry *geometry=NULL; size_t count=0;
        if (!qa_q3_assets_services(assets,&services,&world,&geometry,error) ||
            !qa_q3_assets_model_count(assets,&count,error) || !add(capture,CAPTURE_WORLD,world,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(assets,j,&model,error)) return false;
            if (!model.present) continue;
            if (!add(capture,CAPTURE_WORLD,model.world,error)) return false;
            for (unsigned k=0;k<3;++k)
                if (!add(capture,CAPTURE_MODEL,model.scenes[k],error)) return false;
        }
    }
    return true;
}
bool frontend_capture_begin(qa_frontend *f, frontend_capture **out, qa_error *error)
{
    if (!f || !out || *out || f->stepping || f->preparing || f->round || f->shutdown || f->source_restoring ||
        !f->application || !frontend_owners_idle(f) || !frontend_seat_callbacks_idle(f) ||
        !frontend_save_commands_capture_ready(f) || !frontend_cinematic_capture_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture requires idle actual owners and an empty lease");
    if (!frontend_selected_character_refresh(f,error)) return false;
    frontend_capture *capture=calloc(1,sizeof(*capture));
    if (!capture) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining the frontend capture lease");
    capture->frontend=f; f->capture=capture;
    bool ok=true;
    for (size_t i=0;ok && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        ok=frontend_source_group_read(f,i,&group) && add(capture,CAPTURE_ASSETS,group.assets,error);
    }
    for (size_t i=0;ok && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(f,i,&owner,error) && owner.assets &&
            add(capture,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_equipment_q3_count(f);++i) {
        frontend_equipment_q3_owner_view owner;
        ok=frontend_equipment_q3_at(f,i,&owner,error) && owner.assets &&
            add(capture,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_selected_character_count(f);++i) {
        frontend_selected_character_view owner;
        ok=frontend_selected_character_at(f,i,&owner,error) && owner.assets &&
            add(capture,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view owner;
        ok=frontend_selected_effects_at(f,i,&owner,error) && owner.assets &&
            add(capture,CAPTURE_ASSETS,owner.assets,error);
    }
    for (size_t i=0;ok && i<frontend_equipment_gear_count(f);++i) {
        frontend_equipment_gear_owner_view owner;
        ok=frontend_equipment_gear_at(f,i,&owner,error) && owner.assets &&
            add(capture,CAPTURE_ASSETS,owner.assets,error);
    }
    /* Registry entry preflights strict child idle, so it precedes child tokens. */
    for (size_t i=0;ok && i<capture->count;++i) ok=hold(capture->rows+i,error);
    size_t held=capture->count;
    ok=ok && heaps(capture,error) && registry_children(capture,error);
    for (size_t i=held;ok && i<capture->count;++i) ok=hold(capture->rows+i,error);
    if (!ok) {
        frontend_capture_end(capture);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend capture has an unqualified actual owner");
        return false;
    }
    *out=capture; return true;
}
void frontend_capture_end(frontend_capture *capture)
{
    if (!capture) return;
    for (size_t i=capture->count;i>0;--i) {
        capture_row *row=capture->rows+i-1;
        if (!row->held) continue;
        switch (row->kind) {
        case CAPTURE_ASSETS: qa_q3_assets_capture_end((qa_q3_presentation_assets *)row->owner); break;
        case CAPTURE_IMAGES: qa_scene_resources_capture_end(row->token.images); break;
        case CAPTURE_LIBRARY: qa_material_library_capture_end(row->owner); break;
        case CAPTURE_FONTS: qa_font_library_capture_end(row->token.fonts); break;
        case CAPTURE_ORDER: qa_material_order_capture_end(row->owner); break;
        case CAPTURE_WORLD: qa_scene_world_capture_end(row->token.world); break;
        case CAPTURE_MODEL: qa_scene_model_capture_end(row->token.model); break;
        }
    }
    if (capture->frontend->capture==capture) capture->frontend->capture=NULL;
    free(capture->rows); free(capture);
}
static const void *owner_at(const frontend_capture *capture, capture_kind kind, size_t ordinal)
{
    if (!capture || capture->frontend->capture!=capture) return NULL;
    for (size_t i=0;i<capture->count;++i)
        if (capture->rows[i].kind==kind && !ordinal--) return capture->rows[i].owner;
    return NULL;
}
const qa_scene_resources *frontend_capture_images_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_IMAGES,i); }
const qa_material_library *frontend_capture_library_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_LIBRARY,i); }
const qa_font_library *frontend_capture_fonts_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_FONTS,i); }
qa_q3_presentation_assets *frontend_capture_assets_at(const frontend_capture *c,size_t i)
{ return (qa_q3_presentation_assets *)owner_at(c,CAPTURE_ASSETS,i); }
const qa_scene_world *frontend_capture_world_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_WORLD,i); }
const qa_scene_model *frontend_capture_model_at(const frontend_capture *c,size_t i)
{ return owner_at(c,CAPTURE_MODEL,i); }
