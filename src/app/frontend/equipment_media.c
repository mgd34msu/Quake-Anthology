#include "equipment_media_private.h"
#include "equipment_held_stock.h"
#include "equipment_icon.h"
#include "../application/equipment_runtime.h"
#include "qa/application_equipment_content.h"
#include "../application/guest_q3_components.h"
#include "shared_resource_policy.h"
#include "renderer_materials.h"
#include "qa/media_library_prepare.h"
#include <stdio.h>

bool frontend_equipment_media_namespace_current(const qa_frontend *frontend,
    const frontend_equipment_media *row)
{
    if(row->source_slot) {
        application_q3_component_publication source;bool found=false;
        if(!frontend||!frontend->application||row->gear_namespace||row->gear_service_owner||
            !application_q3_components_checkpoint_publication_read(frontend->application,row->provider,&source,&found,NULL)||
            !found||source.generation!=row->source_generation||!source.product||source.product->family!=row->family||
            !qa_vfs_lookup_equal(source.content,row->owner.mounts))return false;
        for(size_t i=0;i<application_q3_component_item_definition_count(source.game);++i) {
            qa_item_admission item;qa_bytes icon,held;
            if(!application_q3_component_item_definition(source.game,i,&item,&icon,&held,NULL))return false;
            if(item.definition.item==row->item)return held.size==row->source_held.size&&
                (!held.size||!memcmp(held.data,row->source_held.data,held.size))&&
                icon.size==row->source_icon.size&&(!icon.size||!memcmp(icon.data,row->source_icon.data,icon.size));
        }
        return false;
    }
    if (!row->gear_namespace) {
        if (row->gear_service_owner) return false;
        if (row->family != QA_GAME_Q3) return true;
        qa_q3_product product;
        if (!frontend || !frontend->application || !qa_application_equipment_q3_product_read(
                frontend->application, row->provider, &product, NULL)) return false;
        const char *item = qa_strings_cstr(qa_session_strings(
            qa_application_session(frontend->application)), row->item);
        size_t count = 0; const qa_q3_item *items = qa_q3_items(product, &count);
        for (size_t i = 0; item && i < count; ++i) {
            if (items[i].kind != QA_Q3_ITEM_WEAPON || !items[i].model ||
                strcmp(items[i].model, row->view_path)) continue;
            const char *name = qa_q3_weapon_identity_name((qa_q3_weapon)items[i].tag);
            char key[64];
            if (name) {
                snprintf(key, sizeof(key), "q3:weapon/%s", name);
                if (!strcmp(item, key)) return true;
            }
        }
        return false;
    }
    qa_application_equipment_content content;
    if (row->family != QA_GAME_Q3 || !frontend || !frontend->application ||
        !qa_application_equipment_content_read(frontend->application, row->gear_namespace, &content, NULL) ||
        content.selected_owner != row->provider || content.service_owner != row->gear_service_owner ||
        !qa_vfs_lookup_equal(content.files, row->owner.mounts)) return false;
    application_equipment_runtime *runtime = frontend->application->equipment_runtime;
    application_equipment_runtime_source source;
    return application_equipment_runtime_source_read(runtime, content.selected_owner, &source, NULL) &&
        source.gear_owner == row->gear_namespace && source.weapon_item == row->item && source.definition &&
        source.definition->presentation.view_model &&
        !strcmp(source.definition->presentation.view_model, row->view_path);
}

static bool movie_current(void *context,const frontend_material_movie_source *source)
{
    frontend_equipment_media *row=context;
    return row&&row->source_slot&&row->frontend&&source&&source->frontend==row->frontend&&
        source->context==row&&source->current==movie_current&&source->files==row->owner.mounts&&
        source->images==row->owner.images&&source->materials==row->owner.materials&&source->media==row->media&&
        frontend_equipment_media_namespace_current(row->frontend,row);
}

frontend_material_movie_source frontend_equipment_media_movie_source(frontend_equipment_media *row)
{
    return (frontend_material_movie_source){.frontend=row->frontend,.files=row->owner.mounts,
        .images=row->owner.images,.materials=row->owner.materials,.media=row->media,
        .context=row,.current=movie_current};
}

bool frontend_equipment_media_dispose(frontend_equipment_media *media,qa_error *error)
{
    if(media->movies) {
        frontend_material_movie_source source=frontend_equipment_media_movie_source(media);
        if(!media->frontend->source_restoring&&
            !frontend_renderer_materials_adopt_movies(media->frontend,&source,&media->movies,&media->media,error))return false;
        if(!frontend_material_movies_destroy(&media->movies,error))return false;
    }
    qa_media_library_destroy(media->media);media->media=NULL;
    qa_scene_model_destroy(media->held_scene);
    frontend_held_model_free(&media->held);
    frontend_model_release(media->held_lease);
    if(media->source_model){qa_model_free(media->source_model);free(media->source_model);}
    frontend_held_declaration_free(&media->declaration);
    qa_resource_release((qa_resource *)media->view.resource);
    qa_resource_release((qa_resource *)media->held_parent.resource);
    if(media->source_slot){
        qa_material_library_destroy(media->owner.materials);
        qa_scene_resources_destroy(media->owner.images);
        qa_vfs_destroy(media->owner.mounts);
    }
    qa_buffer_free(&media->source_held);
    qa_buffer_free(&media->source_icon);
    qa_resource_release(media->icon_source);
    free(media->saved_parent_path); free(media->view_path); free(media);return true;
}

bool frontend_equipment_idle(const qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return true;
    if (frontend->equipment->admitting) return false;
    for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next)
        if (row->users || (row->held_scene && !qa_scene_model_idle(row->held_scene)) ||
            (row->movies&&!frontend_material_movies_idle(row->movies)) ||
            (row->media&&!qa_media_library_idle(row->media))) return false;
    return true;
}

bool frontend_equipment_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_equipment_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media has an active real scene or admission");
    if (!frontend || !frontend->equipment) return true;
    frontend->equipment->admitting=true;
    while(frontend->equipment->media) {
        frontend_equipment_media *row=frontend->equipment->media,*next=row->next;
        if(!frontend_equipment_media_dispose(row,error)){frontend->equipment->admitting=false;return false;}
        frontend->equipment->media=next;
    }
    frontend->equipment->tail = NULL;
    frontend->equipment->admitting=false;
    return true;
}

bool frontend_equipment_media_prune(qa_frontend *f,qa_error *error)
{
    if(!f||!f->application||f->source_restoring||f->capture||f->resource_inventory||
        !frontend_equipment_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment cache retirement requires its returned live media owners");
    if(!f->equipment)return true;
    f->equipment->admitting=true;
    bool okay=true;
    frontend_equipment_media **slot=&f->equipment->media;
    while(*slot) {
        frontend_equipment_media *row=*slot;
        if(!row->source_slot){slot=&row->next;continue;}
        application_q3_component_publication source;bool found=false;
        if(!application_q3_components_checkpoint_publication_read(f->application,row->provider,&source,&found,error)) {
            okay=false;break;
        }
        if(found&&source.generation==row->source_generation&&(row->bound||row->restoring)) {
            if(!frontend_equipment_media_namespace_current(f,row)) {
                okay=frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment cache changed its retained component declaration");break;
            }
            slot=&row->next;continue;
        }
        frontend_equipment_media *next=row->next;
        if(!frontend_equipment_media_dispose(row,error)){okay=false;break;}
        *slot=next;
    }
    f->equipment->tail=NULL;
    for(frontend_equipment_media *row=f->equipment->media;row;row=row->next)f->equipment->tail=row;
    f->equipment->admitting=false;return okay;
}

void frontend_equipment_destroy(qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return;
    qa_error error = {0};
    if (!frontend_equipment_retire(frontend, &error)) return;
    free(frontend->equipment); frontend->equipment = NULL;
}

static bool held_declaration(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media *row, qa_error *error)
{
    size_t length = strlen(view->view_model);
    if (length > SIZE_MAX - sizeof(".held.json"))
        return frontend_fail(error, QA_ERROR_MEMORY, "Equipment declaration path exceeds address space");
    char *path = malloc(length + sizeof(".held.json"));
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual equipment declaration path");
    memcpy(path, view->view_model, length);
    memcpy(path + length, ".held.json", sizeof(".held.json"));
    bool found = false;
    uint64_t size;
    bool ok = qa_vfs_probe(row->owner.mounts, path, &found, &size, error);
    if (ok && found) {
        qa_resource *source = NULL;
        ok = qa_vfs_acquire(row->owner.mounts, path, &source, NULL, error);
        if (ok) ok = frontend_held_declaration_read(source, &row->declaration, error);
        qa_resource_release(source);
    } else if (ok) {
        const char *item = view->item ?
            qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), view->item) : NULL;
        ok = frontend_held_stock(view->family, view->view_model, item, &row->declaration, &found, error);
        if (ok && !found)
            ok = frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected equipment has no authored held declaration");
    }
    free(path);
    return ok;
}

static bool held_model(qa_frontend *frontend, frontend_equipment_media *row, qa_error *error)
{
    if (row->declaration.none) return true;
    const char *path = row->declaration.path;
    bool found = false;
    uint64_t size;
    if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    if (!found && row->declaration.fallback) {
        path = row->declaration.fallback;
        if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    }
    if (!found) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Actual source held model is absent");
    if (!frontend_visual_model_acquire(frontend, row->provider, row->family, path, NULL,
            &row->held_parent, error)) return false;
    qa_resource_retain((qa_resource *)row->held_parent.resource);
    if (row->family == QA_GAME_Q2 && !row->declaration.source) {
        bool known;
        if (!frontend_held_stock_q2_grip(row->held_parent.resource,
                &row->declaration.grip, &known, error)) return false;
    }
    if (!frontend_held_model_prepare(&row->declaration, row->held_parent.resource,
            row->held_parent.model, &row->held, error)) return false;
    const qa_scene_image_options *options = qa_scene_model_image_options(row->held_parent.scene);
    if (!options) return frontend_fail(error, QA_ERROR_FORMAT, "Held parent has no actual scene image policy");
    return qa_scene_model_create(row->held.model, row->owner.images, row->owner.materials,
        options, &row->held_scene, error) &&
        qa_scene_model_source_resource_bind(row->held_scene, row->held_parent.resource, error);
}

static bool prepare_media(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_held_declaration *authored, frontend_equipment_media **out, qa_error *error)
{
    if (!frontend || !view || !out || !view->selected || !view->view_model || !view->view_model[0] ||
        (view->family == QA_GAME_Q3 && (!authored || !authored->source)) ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Foreign equipment media requires its actual selected source observation");
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission requires idle physical scenes");
    if (!frontend->equipment) {
        frontend->equipment = calloc(1, sizeof(*frontend->equipment));
        if (!frontend->equipment) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment media owner");
    }
    for (frontend_equipment_media *row = frontend->equipment->media; row; row = row->next)
        if (row->provider == view->provider && row->family == view->family && row->item == view->item &&
            row->gear_namespace == view->gear_namespace && row->gear_service_owner == view->gear_service_owner &&
            !strcmp(row->view_path, view->view_model) && (view->family == QA_GAME_Q3 ||
                !view->view_source || row->view.resource == view->view_source)) {
            *out = row; return true;
        }
    frontend_equipment_media *row = calloc(1, sizeof(*row));
    if (!row) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating physical selected equipment media row");
    row->provider = view->provider; row->family = view->family; row->item = view->item;
    row->gear_namespace = view->gear_namespace; row->gear_service_owner = view->gear_service_owner;
    size_t length = strlen(view->view_model) + 1;
    row->view_path = malloc(length);
    if (!row->view_path) { (void)frontend_equipment_media_dispose(row,error); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected model identity"); }
    memcpy(row->view_path, view->view_model, length);
    frontend->equipment->admitting = true;
    bool ok = frontend_visual_media_acquire(frontend, view->provider, view->family, &row->owner, error);
    if (ok && view->family != QA_GAME_Q3)
        ok = frontend_visual_model_acquire(frontend, view->provider, view->family, view->view_model,
            view->view_source, &row->view, error);
    if (ok) {
        qa_resource_retain((qa_resource *)row->view.resource);
        if (authored) { row->declaration = *authored; *authored = (frontend_held_declaration){0}; }
        else ok = held_declaration(frontend, view, row, error);
        if (ok) ok = held_model(frontend, row, error);
    }
    if (ok && (!qa_application_equipment_current(frontend->application, view) ||
        !frontend_equipment_media_namespace_current(frontend, row)))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission lost its actual selected source owner");
    frontend->equipment->admitting = false;
    if (!ok) { (void)frontend_equipment_media_dispose(row,error); return false; }
    row->bound = true;
    if (frontend->equipment->tail) frontend->equipment->tail->next = row;
    else frontend->equipment->media = row;
    frontend->equipment->tail = row;
    *out = row;
    return true;
}

bool frontend_equipment_media_prepare(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media **out, qa_error *error)
{ return prepare_media(frontend, view, NULL, out, error); }

static bool source_media_prepare(qa_frontend *f,const qa_application_equipment_view *view,
    frontend_equipment_media **out,bool *authored,qa_error *error)
{
    if(!f||!view||!out||*out||!authored||!view->source_slot||!view->selected||
        !qa_application_equipment_current(f->application,view)||f->resource_inventory||
        (f->equipment&&f->equipment->admitting))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source held admission requires its actual component binding");
    *authored=false;
    if(!view->source_held.size&&!view->source_icon.size)return true;
    for(frontend_equipment_media *row=f->equipment?f->equipment->media:NULL;row;row=row->next)
        if(row->held_scene&&!qa_scene_model_idle(row->held_scene))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source held admission retains an active scene operation");
    if(!f->equipment){f->equipment=calloc(1,sizeof(*f->equipment));
        if(!f->equipment)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining source held media owner");}
    for(frontend_equipment_media *row=f->equipment->media;row;row=row->next)
        if(row->source_slot&&row->provider==view->provider&&row->item==view->item&&
            row->source_generation==view->source_generation&&row->source_held.size==view->source_held.size&&
            (!view->source_held.size||!memcmp(row->source_held.data,view->source_held.data,view->source_held.size))&&
            row->source_icon.size==view->source_icon.size&&
            (!view->source_icon.size||!memcmp(row->source_icon.data,view->source_icon.data,view->source_icon.size))) {
            if(!row->bound||!frontend_equipment_media_namespace_current(f,row))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained held model lost its actual source declaration");
            *out=row;*authored=true;return true;
        }
    frontend_equipment_media *row=calloc(1,sizeof(*row));
    if(!row)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining declared component held model");
    row->source_slot=true;row->source_generation=view->source_generation;
    row->frontend=f;
    row->provider=view->provider;row->family=view->family;row->item=view->item;
    f->equipment->admitting=true;
    bool ok=true;
    if(view->source_held.size){row->source_held.data=malloc(view->source_held.size);
        ok=row->source_held.data!=NULL;
        if(ok){row->source_held.size=view->source_held.size;memcpy(row->source_held.data,view->source_held.data,view->source_held.size);
            ok=frontend_held_declaration_value(view->source_held,&row->declaration,error);}
        else frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source held declaration");}
    else row->declaration.none=true;
    if(ok&&view->source_icon.size){row->source_icon.data=malloc(view->source_icon.size);
        ok=row->source_icon.data!=NULL;
        if(ok){row->source_icon.size=view->source_icon.size;memcpy(row->source_icon.data,view->source_icon.data,view->source_icon.size);}
        else frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source icon declaration");}
    row->view_path=calloc(1,1);
    row->owner=(frontend_visual_owner_view){.owner=view->provider,.family=view->family==QA_GAME_Q1?QA_SCENE_Q1:
        view->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3};
    if(ok)row->owner.mounts=qa_vfs_clone(view->view_content,error);
    if(ok)row->owner.images=row->owner.mounts?qa_scene_resources_create(row->owner.mounts,error):NULL;
    if(ok)ok=row->view_path&&row->owner.images&&frontend_image_policy_initialize(f,row->owner.images,error);
    if(ok)row->owner.materials=qa_material_library_create(row->owner.images,f->order,error);
    if(ok)ok=row->owner.materials!=NULL;
    if(ok)row->media=qa_media_library_create(row->owner.images,error);
    if(ok)ok=row->media!=NULL;
    if(ok){frontend_material_movie_source source=frontend_equipment_media_movie_source(row);
        ok=frontend_material_movies_create(&source,&row->movies,error);}
    qa_scene_image_options images={.family=row->owner.family,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.usage=QA_IMAGE_USAGE_SKIN,.transparent_index=-1};
    if(ok)ok=qa_material_library_load_scripts(row->owner.materials,row->owner.mounts,&images,error)&&
        frontend_material_remaps(f,row->owner.materials,error);
    if(ok&&view->source_icon.size)ok=frontend_equipment_icon_load(view->source_icon,row->owner.family,
        row->owner.mounts,row->owner.images,row->owner.materials,&row->icon,&row->icon_source,error);
    if(ok&&!row->declaration.none) {
        const char *path=row->declaration.path;bool found=false;uint64_t size;
        ok=qa_vfs_probe(row->owner.mounts,path,&found,&size,error);
        if(ok&&!found&&row->declaration.fallback){path=row->declaration.fallback;ok=qa_vfs_probe(row->owner.mounts,path,&found,&size,error);}
        qa_resource *resource=NULL;
        if(ok&&!found)ok=frontend_fail(error,QA_ERROR_NOT_FOUND,"Actual component held model is absent");
        if(ok)ok=qa_vfs_acquire(row->owner.mounts,path,&resource,NULL,error);
        row->held_parent.resource=resource;
        if(ok){row->source_model=calloc(1,sizeof(*row->source_model));
            if(!row->source_model)ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining component held model arrays");}
        if(ok)ok=qa_model_load(qa_resource_bytes(resource),row->source_model,error);
        if(ok){size_t length=strlen(path)+1;row->saved_parent_path=malloc(length);
            if(!row->saved_parent_path)ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual component held path");
            else memcpy(row->saved_parent_path,path,length);}
        row->held_parent.path=row->saved_parent_path;row->held_parent.model=row->source_model;
        if(ok)ok=frontend_held_model_prepare(&row->declaration,resource,row->source_model,&row->held,error)&&
            qa_scene_model_create(row->held.model,row->owner.images,row->owner.materials,&images,&row->held_scene,error)&&
            qa_scene_model_source_resource_bind(row->held_scene,resource,error);
    }
    if(ok)ok=qa_application_equipment_current(f->application,view)&&frontend_equipment_media_namespace_current(f,row);
    f->equipment->admitting=false;
    if(!ok){
        if(!frontend_equipment_media_dispose(row,error)) {
            if(f->equipment->tail)f->equipment->tail->next=row;else f->equipment->media=row;
            f->equipment->tail=row;
        }
        return false;
    }
    row->bound=true;
    if(f->equipment->tail)f->equipment->tail->next=row;else f->equipment->media=row;
    f->equipment->tail=row;*out=row;*authored=true;return true;
}

bool frontend_equipment_media_prepare_source_held(qa_frontend *f,const qa_application_equipment_view *view,
    frontend_equipment_media **out,bool *authored,qa_error *error)
{
    bool present=false;
    if(!authored||!source_media_prepare(f,view,out,&present,error))return false;
    *authored=present&&view->source_held.size!=0;return true;
}
bool frontend_equipment_media_prepare_source_icon(qa_frontend *f,const qa_application_equipment_view *view,
    frontend_equipment_media **out,const qa_material **icon,qa_error *error)
{
    bool present=false;
    if(!icon||*icon||!source_media_prepare(f,view,out,&present,error))return false;
    *icon=present?(*out)->icon:NULL;return true;
}

typedef struct q1_hud_picture {
    const char *item, *classic, *wheel, *ammo;
} q1_hud_picture;
/* Anthology's common HUD uses actual id1/mission-pack Sbar and wwheel assets. */
static const q1_hud_picture q1_hud_pictures[] = {
    {"q1:weapon/axe", NULL, "axe", NULL},
    {"q1:weapon/shotgun", "shotgun", "shotgun1", "sb_shells"},
    {"q1:weapon/supershotgun", "sshotgun", "shotgun2", "sb_shells"},
    {"q1:weapon/nailgun", "nailgun", "nail1", "sb_nails"},
    {"q1:weapon/supernailgun", "snailgun", "nail2", "sb_nails"},
    {"q1:weapon/grenadelauncher", "rlaunch", "rocket1", "sb_rocket"},
    {"q1:weapon/rocketlauncher", "srlaunch", "rocket2", "sb_rocket"},
    {"q1:weapon/lightning", "lightng", "light", "sb_cells"},
    {"q1:weapon/hipnotic:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/hipnotic:mjolnir", "mjolnir", "ui_h_weapon_mjolnir", "sb_cells"},
    {"q1:weapon/hipnotic:proximity", "prox", "ui_h_weapon_gren", "sb_rocket"},
    {"q1:weapon/rogue:lava-nailgun", "r_lava", "ui_r_weapon_lava", "r_ammolava"},
    {"q1:weapon/rogue:lava-supernailgun", "r_superlava", "ui_r_weapon_superlava", "r_ammolava"},
    {"q1:weapon/rogue:multi-grenade", "r_gren", "ui_r_weapon_gren", "r_ammomulti"},
    {"q1:weapon/rogue:multi-rocket", "r_multirock", "ui_r_weapon_multirock", "r_ammomulti"},
    {"q1:weapon/rogue:plasma", "r_plasma", "ui_r_weapon_plasma", "r_ammoplasma"},
    {"q1:weapon/mg3:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/mg3:mjolnir", NULL, "axe", "sb_cells"}
};
static bool native_icon_declaration(qa_frontend *f,
    const qa_application_equipment_view *view, char key[192], char declaration[256], bool *present, qa_error *error)
{
    *present = false;
    if (!f || !view || view->source_slot ||
        !qa_application_equipment_current(f->application, view))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icon requires its actual equipment source");
    if (view->family != QA_GAME_Q1 || !view->item) return true;
    const char *name = qa_strings_cstr(qa_session_strings(qa_application_session(f->application)), view->item);
    const q1_hud_picture *picture = NULL;
    for (size_t i = 0; name && i < sizeof(q1_hud_pictures) / sizeof(*q1_hud_pictures); ++i)
        if (!strcmp(name, q1_hud_pictures[i].item)) { picture = q1_hud_pictures + i; break; }
    if (!picture) return true;
    const qa_launch_snapshot *launch = qa_application_launch(f->application);
    const char *instance = qa_application_provider_instance(f->application, view->provider);
    const qa_launch_instance *source = instance ? qa_launch_snapshot_find(launch, instance) : NULL;
    const qa_product *product = source ? qa_catalog_product(qa_launch_snapshot_catalog(launch), source->selection.product) : NULL;
    if (!product || product->family != view->family)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icon lost its actual product declaration");
    char path[128], lump[64] = "";
    if (product->edition == QA_EDITION_RERELEASE) {
        snprintf(path, sizeof(path), "gfx/weapons/%s%s_2.lmp",
            !strncmp(picture->wheel, "ui_", 3) ? "" : "ww_", picture->wheel);
    } else {
        const char *icon = picture->classic ? picture->classic : picture->ammo;
        if (!icon) return true;
        snprintf(path, sizeof(path), "%s", "gfx.wad");
        snprintf(lump, sizeof(lump), "%s%s", picture->classic && strncmp(icon, "r_", 2) ? "inv2_" : "", icon);
    }
    if (!frontend_equipment_icon_key(path,lump,key,192,error)) return false;
    int size = lump[0] ? snprintf(declaration, 256,
            "{\"kind\":\"wad-picture\",\"path\":\"%s\",\"lump\":\"%s\"}", path, lump) :
            snprintf(declaration, 256, "{\"kind\":\"image\",\"path\":\"%s\"}", path);
    if (size < 0 || size >= 256)
        return frontend_fail(error, QA_ERROR_MEMORY, "Native HUD icon declaration exceeds its actual extent");
    *present = true; return true;
}
bool frontend_equipment_media_native_icon_prepare(qa_frontend *f,
    const qa_application_equipment_view *view, const qa_material **out, qa_error *error)
{
    char key[192], declaration[256]; bool present;
    if (!out || *out || !native_icon_declaration(f, view, key, declaration, &present, error)) return false;
    if (!present) return true;
    frontend_visual_owner_view media;
    if (!frontend_visual_media_acquire(f, view->provider, view->family, &media, error)) return false;
    const qa_material *material = qa_material_find(media.materials, key);
    if (!material) {
        qa_resource *resource = NULL;
        bool okay = frontend_equipment_icon_load((qa_bytes){(const uint8_t *)declaration, strlen(declaration)},
            media.family, media.mounts, media.images, media.materials, &material, &resource, error);
        qa_resource_release(resource);
        if (!okay) return false;
    }
    if (!qa_application_equipment_current(f->application, view) ||
        !qa_vfs_lookup_equal(qa_application_provider_files(f->application, view->provider), media.mounts))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icon changed its actual equipment or media owner");
    *out = material; return true;
}
bool frontend_equipment_media_native_icon_read(qa_frontend *f,
    const qa_application_equipment_view *view, const qa_material **out, qa_error *error)
{
    char key[192], declaration[256]; bool present;
    if (!out || *out || !native_icon_declaration(f, view, key, declaration, &present, error)) return false;
    if (!present) return true;
    frontend_visual_owner_view media;
    if (!frontend_visual_media_read(f, view->provider, view->family, &media, error)) return false;
    *out = qa_material_find(media.materials, key);
    if (*out) return true;
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icon has not completed its actual media preparation");
}

bool frontend_q1_faces_prepare(qa_vfs *files, qa_scene_resources *images,
    qa_material_library *materials, qa_error *error)
{
    static const char *const faces[] = {"face1","face_p1","face2","face_p2","face3","face_p3",
        "face4","face_p4","face5","face_p5","face_invis","face_invul2","face_inv2","face_quad"};
    for (size_t i=0;i<sizeof(faces)/sizeof(*faces);++i) {
        char key[64], declaration[128];
        if (!frontend_equipment_icon_key("gfx.wad",faces[i],key,sizeof(key),error)) return false;
        if (qa_material_find(materials,key)) continue;
        snprintf(declaration,sizeof(declaration),
            "{\"kind\":\"wad-picture\",\"path\":\"gfx.wad\",\"lump\":\"%s\"}",faces[i]);
        const qa_material *material=NULL; qa_resource *resource=NULL;
        bool okay=frontend_equipment_icon_load((qa_bytes){(const uint8_t *)declaration,strlen(declaration)},
            QA_SCENE_Q1,files,images,materials,&material,&resource,error);
        qa_resource_release(resource);
        if (!okay) return false;
    }
    return true;
}
bool frontend_q1_face_read(const qa_material_library *materials, const char *lump,
    const qa_scene_image **out, qa_error *error)
{
    char key[64];
    if (!frontend_equipment_icon_key("gfx.wad",lump,key,sizeof(key),error)) return false;
    const qa_material *material=qa_material_find(materials,key);
    if (!material || !material->stage_count || !material->stages[0].image_count ||
        !material->stages[0].images[0])
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 face has not completed its actual media preparation");
    *out=material->stages[0].images[0]; return true;
}
bool frontend_equipment_media_q1_faces_prepare(qa_frontend *f, qa_actor_owner provider, qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_acquire(f,provider,QA_GAME_Q1,&media,error) &&
        frontend_q1_faces_prepare(media.mounts,media.images,media.materials,error);
}
bool frontend_equipment_media_q1_face_read(qa_frontend *f, qa_actor_owner provider,
    const char *lump, const qa_scene_image **out, qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_read(f,provider,QA_GAME_Q1,&media,error) &&
        frontend_q1_face_read(media.materials,lump,out,error);
}

bool frontend_equipment_media_source_icon_read(const qa_frontend *f,
    const qa_application_equipment_view *view,const qa_material **icon,qa_error *error)
{
    if(!f||!view||!icon||*icon||!view->source_slot||!view->selected||
        !qa_application_equipment_current(f->application,view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon read requires its actual selected component binding");
    if(!view->source_icon.size)return true;
    for(const frontend_equipment_media *row=f->equipment?f->equipment->media:NULL;row;row=row->next) {
        if(!row->source_slot||row->provider!=view->provider||row->family!=view->family||
            row->item!=view->item||row->source_generation!=view->source_generation||
            row->source_icon.size!=view->source_icon.size||
            memcmp(row->source_icon.data,view->source_icon.data,view->source_icon.size)||
            row->source_held.size!=view->source_held.size||
            (view->source_held.size&&memcmp(row->source_held.data,view->source_held.data,view->source_held.size)))continue;
        if(!row->bound||!row->icon||!frontend_equipment_media_namespace_current(f,row)||
            !qa_application_equipment_current(f->application,view))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon lost its actual prepared media receipt");
        *icon=row->icon;return true;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon has not completed its actual media preparation");
}

bool frontend_equipment_media_prepare_q3_held(qa_frontend *frontend,
    const qa_application_equipment_view *view, frontend_equipment_media **out,
    bool *authored, qa_error *error)
{
    if (!frontend || !view || !out || *out || !authored || view->family != QA_GAME_Q3 ||
        !view->selected || !view->view_model || !view->view_model[0] ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires its actual selected source and empty output");
    *authored = false;
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires idle physical scenes");
    for (frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next) {
        if (row->provider == view->provider && row->family == view->family && row->item == view->item &&
            row->gear_namespace == view->gear_namespace && row->gear_service_owner == view->gear_service_owner &&
            !strcmp(row->view_path, view->view_model)) {
            if (!row->bound || !row->declaration.source)
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 held media lost its retained authored declaration");
            *out = row; *authored = true; return true;
        }
    }
    frontend_visual_owner_view owner;
    if (!frontend_visual_media_acquire(frontend, view->provider, view->family, &owner, error)) return false;
    size_t length = strlen(view->view_model);
    if (length > SIZE_MAX - sizeof(".held.json"))
        return frontend_fail(error, QA_ERROR_MEMORY, "Q3 held declaration path exceeds address space");
    char *path = malloc(length + sizeof(".held.json"));
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q3 held declaration path");
    memcpy(path, view->view_model, length);
    memcpy(path + length, ".held.json", sizeof(".held.json"));
    bool found = false;
    uint64_t size;
    bool okay = qa_vfs_probe(owner.mounts, path, &found, &size, error);
    qa_resource *resource = NULL; frontend_held_declaration declaration = {0};
    if (okay && found) okay = qa_vfs_acquire(owner.mounts, path, &resource, NULL, error) &&
        frontend_held_declaration_read(resource, &declaration, error);
    free(path); qa_resource_release(resource);
    if (okay && found) okay = prepare_media(frontend, view, &declaration, out, error);
    frontend_held_declaration_free(&declaration);
    if (okay && !qa_application_equipment_current(frontend->application, view))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held probe retired its actual selected source");
    if (okay) *authored = found;
    return okay;
}

bool frontend_equipment_media_read(const frontend_equipment_media *row,
    frontend_equipment_media_view *out)
{
    if (!row || !out) return false;
    *out = (frontend_equipment_media_view){row->provider, row->family, row->item,
        row->view_path, row->owner, row->view, row->held_parent, &row->declaration,
        &row->held, row->held_scene, row->gear_namespace, row->gear_service_owner,
        row->source_slot,row->source_generation,{row->source_held.data,row->source_held.size},
        {row->source_icon.data,row->source_icon.size},row->icon,row->media,row->movies};
    return true;
}

bool frontend_equipment_media_retain(frontend_equipment_media *row, qa_error *error)
{
    if (!row || row->users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media retain exceeds its actual holder lifetime");
    ++row->users;
    return true;
}

void frontend_equipment_media_release(frontend_equipment_media *row)
{
    if (row) --row->users;
}

size_t frontend_equipment_media_count(const qa_frontend *frontend)
{
    size_t count = 0;
    if (frontend && frontend->equipment)
        for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next) ++count;
    return count;
}

bool frontend_equipment_media_at(const qa_frontend *frontend, size_t ordinal,
    frontend_equipment_media_view *out)
{
    const frontend_equipment_media *row = frontend && frontend->equipment ? frontend->equipment->media : NULL;
    while (row && ordinal) { row = row->next; --ordinal; }
    return frontend_equipment_media_read(row, out);
}

size_t frontend_equipment_movie_count(const qa_frontend *f)
{
    size_t count=0;
    for(const frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;row;row=row->next)
        count+=row->source_slot;
    return count;
}
bool frontend_equipment_movie_at(const qa_frontend *f,size_t index,size_t *physical)
{
    size_t ordinal=0;
    for(const frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;row;row=row->next,++ordinal)
        if(row->source_slot){if(!index){if(!physical)return false;*physical=ordinal;return true;}--index;}
    return false;
}
bool frontend_equipment_movie_source_read(qa_frontend *f,size_t ordinal,
    frontend_material_movie_source *out,qa_error *error)
{
    frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;
    while(row&&ordinal){row=row->next;--ordinal;}
    if(!row||!out||!row->source_slot||row->frontend!=f||!row->media||
        !frontend_equipment_media_namespace_current(f,row))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment shader movie lost its actual component media owner");
    *out=frontend_equipment_media_movie_source(row);
    return (f->source_restoring&&row->restoring&&!row->movies)||
        (row->movies&&frontend_material_movies_current(row->movies));
}
bool frontend_equipment_movies_restore(qa_frontend *f,size_t ordinal,
    const frontend_material_movies_refs *refs,qa_bytes bytes,qa_error *error)
{
    frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;
    size_t at=ordinal;
    while(row&&at){row=row->next;--at;}
    frontend_material_movie_source source;
    return f&&f->source_restoring&&row&&row->restoring&&!row->movies&&
        frontend_equipment_movie_source_read(f,ordinal,&source,error)&&
        frontend_material_movies_restore(&source,refs,bytes,&row->movies,error);
}
