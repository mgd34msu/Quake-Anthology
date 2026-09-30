#include "internal.h"
#include <stdio.h>

/* Derive the source's fixed grid from the selected native font once. The UI
 * keeps a proper classic charset for fixed cells and a shared Unicode atlas. */
static bool classic_font(qa_frontend *frontend, qa_error *error)
{
    uint8_t pixels[128 * 128 * 4] = {0};
    for (uint32_t code = 0; code < 256; ++code) {
        qa_font_glyph glyph;
        if (!qa_font_find_glyph(frontend->primary, code & 127u, &glyph) || !glyph.visible || !glyph.image || !glyph.image->level_count) continue;
        const qa_scene_image_level *level = &glyph.image->levels[0];
        if (!level->pixels || glyph.image->kind == QA_SCENE_DEPTH32F) continue;
        uint32_t x0 = (uint32_t)(glyph.uv.x * (float)level->width);
        uint32_t y0 = (uint32_t)(glyph.uv.y * (float)level->height);
        uint32_t width = (uint32_t)((glyph.uv.z - glyph.uv.x) * (float)level->width);
        uint32_t height = (uint32_t)((glyph.uv.w - glyph.uv.y) * (float)level->height);
        if (!width || !height || x0 >= level->width || y0 >= level->height || width > level->width - x0 || height > level->height - y0) continue;
        const uint8_t *source = level->pixels;
        for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x) {
            size_t from = ((size_t)(y0 + y * height / 8) * level->width + x0 + x * width / 8) * 4;
            size_t to = ((size_t)(code / 16 * 8 + y) * 128 + code % 16 * 8 + x) * 4;
            memcpy(pixels + to, source + from, 4);
        }
    }
    qa_scene_image *image = NULL;
    qa_scene_image_level level = {128, 128, pixels, sizeof(pixels)};
    if (!qa_scene_image_create(frontend->ui_images, "frontend:classic", QA_SCENE_RGBA8,
        &level, 1, QA_SCENE_CLAMP, QA_SCENE_NEAREST, (qa_scene_vec4){0}, &image, error)) return false;
    bool ok = qa_font_classic_create(frontend->fonts, "frontend:classic", image,
        QA_FONT_TINTED, &frontend->classic, error);
    qa_scene_image_release(image);
    return ok;
}
bool frontend_resources(qa_frontend *frontend, qa_error *error)
{
    frontend->ui_mounts = qa_vfs_create(qa_application_resources(frontend->application), error);
    qa_mount_id mount;
    if (!frontend->ui_mounts || !qa_vfs_mount_directory(frontend->ui_mounts,
        frontend->options.font_directory, QA_ARCHIVE_EXACT, false, &mount, error)) return false;
    frontend->ui_images = qa_scene_resources_create(frontend->ui_mounts, error);
    if (!frontend->ui_images) return false;
    const uint8_t background[] = {8, 8, 12, 235};
    qa_scene_image_level pixel = {1, 1, background, sizeof(background)};
    if (!qa_scene_image_create(frontend->ui_images, "frontend:console-background", QA_SCENE_RGBA8,
        &pixel, 1, QA_SCENE_CLAMP, QA_SCENE_NEAREST, (qa_scene_vec4){0},
        &frontend->console_background, error)) return false;
    frontend->fonts = qa_font_library_create(frontend->ui_mounts, frontend->ui_images, error);
    qa_font_truetype_options font = {.path = frontend->options.font_file, .pixel_size = 24,
        .atlas_width = 1024, .atlas_height = 1024};
    if (!frontend->fonts || !qa_font_truetype_load(frontend->fonts, &font, &frontend->primary, error) ||
        !classic_font(frontend, error)) return false;
    frontend->order = qa_material_order_create(error);
    return frontend->order != NULL;
}
