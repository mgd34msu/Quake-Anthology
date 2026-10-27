/* Port of src/render/scene/world-text.ts. GPL-2.0-or-later. */
#include "qa/render/scene/world_text.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"
void qa_scene_world_text_batches_destroy(qa_scene_world_text_batches *b) {
  if (!b) return;
  free(b->batches); free(b->vertices); free(b->indices); memset(b, 0, sizeof *b);
}
/* Decode one UTF-8 codepoint (TS iterates UTF-16 code points; valid text agrees). */
static size_t utf8_next(const unsigned char *s, size_t remaining, uint32_t *out) {
  if (!remaining) return 0;
  unsigned char c = s[0];
  if (c < 0x80) { *out = c; return 1; }
  if ((c & 0xE0) == 0xC0 && remaining > 1) { *out = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
  if ((c & 0xF0) == 0xE0 && remaining > 2) { *out = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3; }
  if ((c & 0xF8) == 0xF0 && remaining > 3) { *out = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4; }
  *out = c; return 1;
}
static qa_vec4 point_at(qa_scene_view_projector project, qa_vec3 origin, qa_vec3 right, qa_vec3 down, double cf, double rf) {
  return qa_scene_projector_project(project, qa_add3(origin, qa_add3(qa_scale3(right, cf), qa_scale3(down, rf))));
}
qa_draw_batch *qa_prepare_world_text(const qa_world_text *texts, size_t count, qa_scene_camera camera,
                                     qa_scene_font_for_fn font_for, void *font_ctx, bool has_factor,
                                     double distance_cull_factor, qa_scene_world_text_batches *out, size_t *out_count) {
  memset(out, 0, sizeof *out);
  qa_scene_view_projector project = qa_scene_create_view_projector(camera);
  size_t cap_b = 0, cap_v = 0, nv = 0;
  for (size_t ti = 0; ti < count; ti++) {
    const qa_world_text *text = &texts[ti];
    if (text->has_distance_cull_factor
        && text->cell_size < qa_dot3(qa_sub3(text->origin, camera.origin), camera.axis.vectors[0])
             * (has_factor ? distance_cull_factor : text->distance_cull_factor)) continue;
    qa_text_font_selection selected = font_for(font_ctx, text);
    qa_text_font_selection font = selected;
    if (!text->font_selected) { font.is_atlas = false; font.unicode = NULL; }
    qa_axis axis = text->orientation_fixed ? qa_angles_to_axis(text->angles) : camera.axis;
    qa_vec3 right = qa_scale3(axis.vectors[1], -text->cell_size), down = qa_scale3(axis.vectors[2], -text->cell_size);
    /* rows split on '\n' */
    const char *s = text->text; size_t row = 0;
    while (1) {
      const char *nl = strchr(s, '\n');
      size_t line_len = nl ? (size_t)(nl - s) : strlen(s);
      /* count characters (codepoints) */
      uint32_t *codes = malloc((line_len + 1) * sizeof *codes);
      if (!codes) qa_range_error("Allocation failed");
      size_t nchars = 0; size_t pos = 0;
      while (pos < line_len) { size_t adv = utf8_next((const unsigned char *)s + pos, line_len - pos, &codes[nchars]); if (!adv) break; nchars++; pos += adv; }
      for (size_t column = 0; column < nchars; column++) {
        qa_resolved_text_glyph glyph = qa_resolve_text_glyph(&font, codes[column], false);
        if (!glyph.visible) continue;
        qa_text_texture_rect uv = qa_glyph_uv(&glyph);
        if (nv + 4 > cap_v) {
          cap_v = cap_v ? cap_v * 2 : 64; while (nv + 4 > cap_v) cap_v *= 2;
          out->vertices = realloc(out->vertices, cap_v * sizeof *out->vertices);
          if (!out->vertices) qa_range_error("Allocation failed");
        }
        qa_render_vertex *vs = out->vertices + nv; nv += 4;
        double cf0 = (double)column - (double)nchars * 0.5;
        vs[0] = (qa_render_vertex){ point_at(project, text->origin, right, down, cf0 + 0, (double)row + 0), { uv.s, uv.t }, text->color };
        vs[1] = (qa_render_vertex){ point_at(project, text->origin, right, down, cf0 + 1, (double)row + 0), { uv.s2, uv.t }, text->color };
        vs[2] = (qa_render_vertex){ point_at(project, text->origin, right, down, cf0 + 1, (double)row + 1), { uv.s2, uv.t2 }, text->color };
        vs[3] = (qa_render_vertex){ point_at(project, text->origin, right, down, cf0 + 0, (double)row + 1), { uv.s, uv.t2 }, text->color };
        if (out->count == cap_b) {
          cap_b = cap_b ? cap_b * 2 : 16;
          out->batches = realloc(out->batches, cap_b * sizeof *out->batches);
          if (!out->batches) qa_range_error("Allocation failed");
        }
        qa_draw_batch *b = &out->batches[out->count]; memset(b, 0, sizeof *b);
        b->lighting.kind = "vertex"; b->texturing_is_pair = false; b->primitive_is_lines = false;
        b->vertices = vs; b->vertex_count = 4;
        /* shared index block lazily allocated once per batch segment: single block reused */
        if (!out->indices) {
          out->indices = malloc(6 * sizeof(double));
          if (!out->indices) qa_range_error("Allocation failed");
          static const double idx[6] = { 0, 1, 2, 0, 2, 3 }; memcpy(out->indices, idx, sizeof idx);
        }
        b->indices.values = out->indices; b->indices.count = 6;
        b->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
        b->texture.image = glyph.atlas->picture.image.image;
        b->state.blend.source = "src-alpha"; b->state.blend.destination = "one-minus-src-alpha";
        b->state.depth_test = text->depth_test ? "less-equal" : "always";
        b->state.depth_write = false; b->state.alpha_test = "none"; b->state.cull = "none";
        b->state.depth_range[0] = 0; b->state.depth_range[1] = 1; b->state.polygon_offset = NULL;
        out->count++;
        /* vertices pointers stay valid: realloc moves are re-based below by batch index */
        for (size_t k = 0; k + 1 < out->count; k++) (void)k;
      }
      free(codes);
      row++;
      if (!nl) break;
      s = nl + 1;
    }
  }
  /* Rebase vertex pointers after reallocs: each batch holds 4 consecutive. */
  for (size_t k = 0; k < out->count; k++) out->batches[k].vertices = out->vertices + k * 4;
  if (out_count) *out_count = out->count;
  return out->batches;
}
