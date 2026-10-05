#include "menu_art.h"
#include "menu-art-embedded.h"
#include <stdio.h>

static const qa_scene_embedded_image pictures[] = {
    {"engine-menu:background", {qa_menu_background_data, sizeof(qa_menu_background_data)}},
    {"engine-menu:main-background", {qa_menu_main_background_data, sizeof(qa_menu_main_background_data)}},
    {"engine-menu:panel", {qa_menu_panel_data, sizeof(qa_menu_panel_data)}},
    {"engine-menu:focus", {qa_menu_focus_data, sizeof(qa_menu_focus_data)}}
};

static bool picture(qa_frontend *f, const char *name,
    const qa_scene_image **out, qa_error *error)
{
    qa_scene_image *image = NULL;
    bool ok = qa_scene_image_load_embedded(f->ui_images, name, QA_SCENE_CLAMP,
        QA_SCENE_LINEAR, (qa_scene_vec4){0}, &image, error);
    if (ok) *out = image;
    return ok;
}
bool frontend_menu_art_bind(qa_frontend *f, qa_error *error)
{
    return !f->ui_images || qa_scene_resources_bind_embedded_images(f->ui_images, pictures,
        sizeof(pictures) / sizeof(pictures[0]), error);
}
bool frontend_menu_art_create(qa_frontend *f, qa_error *error)
{
    return frontend_menu_art_bind(f, error) &&
        picture(f, "engine-menu:background", &f->menu_art.background, error) &&
        picture(f, "engine-menu:main-background", &f->menu_art.main_background, error) &&
        picture(f, "engine-menu:panel", &f->menu_art.panel, error) &&
        picture(f, "engine-menu:focus", &f->menu_art.focus, error);
}
void frontend_menu_art_destroy(qa_frontend *f)
{
    qa_scene_image_release(f->menu_art.background);
    qa_scene_image_release(f->menu_art.main_background);
    qa_scene_image_release(f->menu_art.panel);
    qa_scene_image_release(f->menu_art.focus);
    f->menu_art = (qa_ui_art){0};
}
bool frontend_menu_art_q1_source(frontend_seat *seat,qa_scene_resources **out,qa_error *error)
{
    qa_frontend *f=seat->frontend;
    const qa_launch_snapshot *launch=qa_application_launch(f->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(launch);
    const qa_product *product=choices?qa_catalog_product(qa_launch_snapshot_catalog(launch),choices->world.geometry):NULL;
    if (!product || product->family!=QA_GAME_Q1 || !f->images)
        return frontend_fail(error,QA_ERROR_NOT_FOUND,"Quake help needs its installed Source content");
    *out=f->images;return true;
}
bool frontend_menu_art_q1_help_page(qa_scene_resources *images,unsigned page,const qa_scene_image **out,qa_error *error)
{
    char path[20];snprintf(path,sizeof(path),"gfx/help%u.lmp",page);
    qa_scene_image_options options={.family=QA_SCENE_Q1,.usage=QA_IMAGE_USAGE_PICTURE,
        .wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_NEAREST,.transparent_index=-1};
    qa_scene_image *image=NULL;
    if (!qa_scene_image_load_exact(images,path,&options,&image,error))return false;
    if (!image)return frontend_fail(error,QA_ERROR_NOT_FOUND,"Quake Source help page is absent");
    *out=image;return true;
}
