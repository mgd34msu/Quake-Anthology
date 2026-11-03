/* Port of src/ui/common/assets.ts. GPL-2.0-or-later. */
#include "qa/ui/common/assets.h"

#include <string.h>

#include "qa/foundation/support.h"
#include "qa/ui/common/art_manifest.h"

typedef struct asset_row {
  qa_resource_id resource;
  const qa_menu_art_file *file;
} asset_row;

qa_image_picture qa_native_ui_art_picture(const qa_native_ui_art *art,
                                          qa_resource_id resource) {
  if (!art->closed)
    for (size_t i = 0; i < 4; i++)
      if (art->resources[i] != NULL && strcmp(art->resources[i], resource) == 0) return art->pictures[i];
  qa_range_error("Unregistered native UI image: %s", resource);
}

void qa_native_ui_art_close(qa_native_ui_art *art) {
  if (art->closed) return;
  art->closed = true;
  for (size_t i = 0; i < art->owned_count; i++)
    qa_scene_image_registry_release(art->images, art->owned[i]);
}

static qa_ui_image_slice slice_of(qa_resource_id resource,
                                  const qa_menu_art_frame *frame) {
  qa_ui_image_slice slice;
  memset(&slice, 0, sizeof slice);
  slice.resource = resource;
  slice.width = frame->region_width;
  slice.height = frame->region_height;
  slice.uv[0] = frame->region_uv[0];
  slice.uv[1] = frame->region_uv[1];
  slice.border_left = frame->border_left;
  slice.border_top = frame->border_top;
  slice.border_right = frame->border_right;
  slice.border_bottom = frame->border_bottom;
  slice.has_border_scale = true;
  slice.border_scale = frame->border_scale;
  return slice;
}

void qa_ui_load_native_art(qa_native_ui_art *art, qa_resource_id font,
                                       qa_scene_image_registry *images,
                                       qa_native_ui_art_read_fn read,
                                       void *read_ctx) {
  qa_ui_load_native_art_selected(art, font, images, read, read_ctx, true, true);
}

void qa_ui_load_native_art_selected(qa_native_ui_art *art, qa_resource_id font,
                                    qa_scene_image_registry *images,
                                    qa_native_ui_art_read_fn read,
                                    void *read_ctx, bool frames,
                                    bool main_background) {
  /* [...(frames ? [background] : []), ...(mainBackground ? [main] : []), ...(frames ? [panel, focus] : [])] */
  asset_row assets[4];
  size_t count = 0;
  if (frames) assets[count++] = (asset_row){ "resource:engine-menu:background", &qa_ui_menu_background };
  if (main_background) assets[count++] = (asset_row){ "resource:engine-menu:main-background", &qa_ui_main_menu_background };
  if (frames) {
    assets[count++] = (asset_row){ "resource:engine-menu:panel", &qa_ui_menu_panel_art.file };
    assets[count++] = (asset_row){ "resource:engine-menu:focus", &qa_ui_menu_focus_art.file };
  }
  memset(art, 0, sizeof *art);
  art->images = images;
  /* Decode every selected image first (TS Promise.all), then validate/register. */
  qa_image_level decoded[4];
  for (size_t i = 0; i < count; i++)
    if (!read(read_ctx, assets[i].file->file, &decoded[i]))
      qa_range_error("Menu image could not be read: %s",
                     assets[i].file->file);
  for (size_t i = 0; i < count; i++) {
    if (decoded[i].width != assets[i].file->width
        || decoded[i].height != assets[i].file->height) {
      qa_native_ui_art_close(art);
      qa_range_error("Menu image dimensions differ from the manifest: %s",
                     assets[i].file->file);
    }
    qa_image_level storage;
    qa_texture_sampling sampling = { "clamp", "linear" };
    const qa_scene_image_handle *handle =
      qa_scene_image_registry_register_generated(
        images, assets[i].file->file,
        qa_scene_rgba_image(decoded[i], &storage), sampling);
    art->owned[art->owned_count++] = handle->image;
    art->resources[i] = assets[i].resource;
    art->pictures[i].name = assets[i].file->file;
    art->pictures[i].image = handle->image;
  }
  static const uint8_t white_pixels[4] = { 255, 255, 255, 255 };
  /* The registration's create operation is queued until the renderer drains
   * it, so the level it points at must outlive this call (constant data). */
  static const qa_image_level white_level = { 1, 1, { white_pixels, 4 } };
  static qa_image_level white_storage;
  qa_texture_sampling nearest = { "clamp", "nearest" };
  const qa_scene_image_handle *white_handle =
    qa_scene_image_registry_register_generated(
      images, "menu-white", qa_scene_rgba_image(white_level, &white_storage),
      nearest);
  art->owned[art->owned_count++] = white_handle->image;
  art->skin = qa_ui_default_skin(font);
  if (frames) {
    art->panel_slice = slice_of("resource:engine-menu:panel",
                               &qa_ui_menu_panel_art);
    art->focus_slice = slice_of("resource:engine-menu:focus",
                               &qa_ui_menu_focus_art);
    art->skin.background = "resource:engine-menu:background";
    art->skin.panel = &art->panel_slice;
    art->skin.focus = &art->focus_slice;
  } else {
    art->skin.background = NULL;
    art->skin.panel = NULL;
    art->skin.focus = NULL;
  }
  art->white.name = "menu-white";
  art->white.image = white_handle->image;
}
