/*
 * Port of src/ui/common/assets.ts: NativeUiArt / loadNativeUiArt.
 * The application supplies its installed-asset reader; the renderer
 * owns all uploads.
 * C accommodations: the TS async read is a synchronous fn + ctx
 * (docs/PORTING.md on async flows); it returns false to fail the
 * whole load (owned images are released first, then the load
 * aborts like the TS throw). The decoded qa_image_level pixels are
 * borrowed by the caller of read for that call; registration copies.
 * Plain TS Errors abort with the identical message.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_ASSETS_H
#define QA_UI_COMMON_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/resources.h"
#include "qa/text/draw2d.h"
#include "qa/ui/common/skin.h"

/** TS read: (path) => Promise<ImageLevel>. */
typedef bool (*qa_native_ui_art_read_fn)(void *ctx, const char *path,
                                         qa_image_level *out);

/** TS NativeUiArt. Owns its registered images until close. */
typedef struct qa_native_ui_art {
  qa_ui_skin skin;
  qa_image_picture white;
  qa_scene_image_registry *images; /* borrowed */
  qa_resource_id resources[4];
  qa_image_picture pictures[4];
  qa_renderer_image owned[5];
  size_t owned_count;
  qa_ui_image_slice panel_slice, focus_slice;
  bool closed;
} qa_native_ui_art;

/* TS NativeUiArt.picture(resource). Aborts (TS Error) when the
 * resource is unregistered or the art is closed. */
qa_image_picture qa_native_ui_art_picture(const qa_native_ui_art *art,
                                          qa_resource_id resource);
/** TS NativeUiArt.close(): idempotent. */
void qa_native_ui_art_close(qa_native_ui_art *art);

/** TS loadNativeUiArt(font, images, read), synchronous in C. Fills *art in
 * place, since art->skin points at art's own slices; do not copy the result. */
void qa_ui_load_native_art(qa_native_ui_art *art, qa_resource_id font,
                                       qa_scene_image_registry *images,
                                       qa_native_ui_art_read_fn read,
                                       void *read_ctx);
/** TS loadNativeUiArt(font, images, read, { frames, mainBackground }): the
 * selection the application and remote application pass ({ frames: false,
 * mainBackground: false } loads only the white image and a frameless skin).
 * qa_ui_load_native_art is the TS default selection (both true). */
void qa_ui_load_native_art_selected(qa_native_ui_art *art, qa_resource_id font,
                                    qa_scene_image_registry *images,
                                    qa_native_ui_art_read_fn read,
                                    void *read_ctx, bool frames,
                                    bool main_background);

#endif
