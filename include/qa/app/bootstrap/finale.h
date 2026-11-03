/* Port of src/app/bootstrap/finale.ts. Banner decode/register and
 * localized text resolution are host steps; event intake, revision
 * gating and the center-string reveal layout are kept here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FINALE_H
#define QA_APP_BOOTSTRAP_FINALE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_source_finale qa_app_source_finale;
typedef struct qa_app_finale_host {
  void *ctx;
  /* Load gfx/finale.lmp for content; error or NULL, sets size. */
  const char *(*load_banner)(void *ctx, qa_content_id content, double *width, double *height);
  /* Localize state text; malloc'd result. */
  const char *(*resolve_message)(void *ctx, qa_content_id content, const char *text, char **out);
  /* Draw surface: dimensions + pic/text callbacks. */
  double width, height;
  void (*draw_pic)(void *ctx, double x, double y, double width, double height);
  double (*text_width)(void *ctx, const char *line, double scale);
  void (*draw_text)(void *ctx, const char *line, double scale, long max_glyphs, double x, double y);
} qa_app_finale_host;
qa_app_source_finale *qa_app_source_finale_create(qa_app_finale_host host);
void qa_app_source_finale_destroy(qa_app_source_finale *finale);
bool qa_app_source_finale_active(const qa_app_source_finale *finale);
void qa_app_source_finale_receive(qa_app_source_finale *finale, const qa_app_sim_presentation_event *events, size_t count);
const char *qa_app_source_finale_prepare(qa_app_source_finale *finale);
void qa_app_source_finale_draw(qa_app_source_finale *finale, double seconds);
/* The draw surface size (TS draw.width/height of the Draw2D passed to draw). */
void qa_app_source_finale_resize(qa_app_source_finale *finale, double width, double height);
/* The active finale's content (TS state.content), NULL when inactive. */
const char *qa_app_source_finale_content(const qa_app_source_finale *finale);
#endif
