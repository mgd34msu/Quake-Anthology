/* Port of src/app/bootstrap/q2-native-hud.ts. ApplicationAssets
 * texture access (assets.ts, other slice) is the host below.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_NATIVE_HUD_H
#define QA_APP_BOOTSTRAP_Q2_NATIVE_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
#include "qa/ui/hud/q2_native.h"
#include "qa/ui/hud/q2_rerelease_layout.h"
/* TS NativeQ2HudRendering. */
typedef struct qa_app_native_q2_hud_rendering {
  qa_native_q2_hud_environment environment;
  qa_resource_id font;
  double font_scale;
} qa_app_native_q2_hud_rendering;
typedef struct qa_app_q2_hud_assets {
  void *ctx;
  /* provider.textures.load(path,{family:"q2",mipmap:false,wrap:"clamp"}):
   * false = TS null; missing_image is textures.missing.image. */
  bool (*load_texture)(void *ctx, qa_content_id content, const char *path, qa_renderer_image *image_out);
  qa_renderer_image (*missing_image)(void *ctx, qa_content_id content);
} qa_app_q2_hud_assets;
typedef void (*qa_app_q2_hud_assert_fn)(void *ctx);
typedef struct qa_app_q2_native_hud qa_app_q2_native_hud;
qa_app_q2_native_hud *qa_app_q2_native_hud_create(void);
void qa_app_q2_native_hud_destroy(qa_app_q2_native_hud *hud);
void qa_app_q2_native_hud_clear(qa_app_q2_native_hud *hud);
/* Borrowed picture or NULL (TS undefined). */
const qa_picture_asset *qa_app_q2_native_hud_picture(qa_app_q2_native_hud *hud, qa_resource_id id);
/* mode "layout-overlay"|"replace-status"; arsenal/environment NULL
 * are TS undefined; assert_current may be NULL. */
void qa_app_q2_native_hud_prepare(qa_app_q2_native_hud *hud, qa_content_id content,
    const qa_app_q2_hud_assets *assets, const qa_native_q2_hud_frame *frame,
    qa_ui_draw_context context, double scale, const char *mode,
    qa_app_q2_hud_assert_fn assert_current, void *assert_ctx,
    const qa_native_q2_hud_arsenal *arsenal, const qa_app_native_q2_hud_rendering *environment);
/* Commands are malloc'd (caller frees array). binding may be NULL. */
size_t qa_app_q2_native_hud_commands(qa_app_q2_native_hud *hud, const qa_native_q2_hud_frame *frame,
    qa_ui_draw_context context, double scale, qa_q2_hud_binding_fn binding, void *binding_ctx,
    const char *mode, const qa_native_q2_hud_arsenal *arsenal, qa_ui_draw_command **out);
#endif
