/*
 * Port of tests/text/truetype.test.ts. The real-font cases need the
 * QuakeEX.kpf corpus (skipped in TS when absent); here a synthetic
 * 442-byte sfnt (glyf simple + composite glyphs, format-4 cmap, kern,
 * COLR/CPAL) stands in, with every expectation taken from running the
 * TypeScript implementation on the same bytes. The boundary and
 * fractional-coverage cases port the TS test verbatim. CID/CFF real
 * font behavior was cross-checked against the TS on system OTFs
 * during the port but is not embedded here. GPL-2.0-or-later.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/text/truetype.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static const uint8_t k_font[] = {
  0,1,0,0,0,10,0,0,0,0,0,0,104,101,97,100,
  0,0,0,0,0,0,0,172,0,0,0,54,104,104,101,97,
  0,0,0,0,0,0,0,226,0,0,0,36,109,97,120,112,
  0,0,0,0,0,0,1,6,0,0,0,6,104,109,116,120,
  0,0,0,0,0,0,1,12,0,0,0,10,99,109,97,112,
  0,0,0,0,0,0,1,22,0,0,0,44,103,108,121,102,
  0,0,0,0,0,0,1,66,0,0,0,42,108,111,99,97,
  0,0,0,0,0,0,1,108,0,0,0,8,107,101,114,110,
  0,0,0,0,0,0,1,116,0,0,0,24,67,79,76,82,
  0,0,0,0,0,0,1,140,0,0,0,28,67,80,65,76,
  0,0,0,0,0,0,1,168,0,0,0,18,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,232,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,3,32,255,56,0,100,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,2,0,1,0,0,0,3,1,244,0,0,
  2,88,0,10,0,20,0,0,0,1,0,3,0,1,0,0,
  0,12,0,4,0,32,0,0,0,4,0,4,0,1,0,0,
  0,66,255,255,0,0,0,65,255,255,255,192,0,1,0,0,
  0,0,0,1,0,0,0,0,1,244,2,188,0,2,0,0,
  49,33,1,1,244,254,12,2,188,0,255,255,0,100,0,50,
  2,88,2,238,0,3,0,1,0,100,0,50,0,0,0,0,
  0,12,0,21,0,0,0,1,0,0,0,20,0,1,0,1,
  0,6,0,0,0,0,0,1,0,2,255,206,0,0,0,1,
  0,0,0,14,0,0,0,20,0,2,0,2,0,0,0,2,
  0,1,0,0,0,1,255,255,0,0,0,1,0,1,0,1,
  0,0,0,14,0,0,10,20,30,255,
};

static const uint8_t k_glyph_coverage[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,32,0,0,0,
  0,0,0,0,0,0,0,0,0,0,197,3,0,0,0,0,
  0,0,0,0,0,0,0,0,255,128,0,0,0,0,0,0,
  0,0,0,0,0,0,255,252,58,0,0,0,0,0,0,0,
  0,0,0,0,255,255,223,14,0,0,0,0,0,0,0,0,
  0,0,255,255,255,164,0,0,0,0,0,0,0,0,0,0,
  255,255,255,255,91,0,0,0,0,0,0,0,0,0,255,255,
  255,255,241,32,0,0,0,0,0,0,0,0,255,255,255,255,
  255,197,3,0,0,0,0,0,0,0,255,255,255,255,255,255,
  128,0,0,0,0,0,0,0,255,255,255,255,255,255,252,58,
  0,0,0,0,0,0,255,255,255,255,255,255,255,223,14,0,
  0,0,0,0,255,255,255,255,255,255,255,255,164,0,0,0,
  0,0,255,255,255,255,255,255,255,255,255,91,0,0,0,0,
  255,255,255,255,255,255,255,255,255,241,32,0,0,0,255,255,
  255,255,255,255,255,255,255,255,197,3,0,0,255,255,255,255,
  255,255,255,255,255,255,255,128,0,0,
};

static void put_be32(uint8_t *p, size_t off, uint32_t v) {
  p[off] = (uint8_t)(v >> 24);
  p[off + 1] = (uint8_t)(v >> 16);
  p[off + 2] = (uint8_t)(v >> 8);
  p[off + 3] = (uint8_t)v;
}

static void put_be16(uint8_t *p, size_t off, uint16_t v) {
  p[off] = (uint8_t)(v >> 8);
  p[off + 1] = (uint8_t)v;
}

static void test_boundaries(void) {
  uint8_t tiny[3] = {0, 0, 0};
  qa_parse_font_result r = qa_truetype_parse_font(tiny, sizeof tiny);
  CHECK(!r.ok);
  CHECK(r.reason != NULL);
  qa_parse_font_result_destroy(&r);

  uint8_t truncated[28] = {0};
  put_be32(truncated, 0, 0x00010000u);
  put_be16(truncated, 4, 1);
  memcpy(truncated + 12, "head", 4);
  put_be32(truncated, 20, 28);
  put_be32(truncated, 24, 54);
  r = qa_truetype_parse_font(truncated, sizeof truncated);
  CHECK(!r.ok);
  qa_parse_font_result_destroy(&r);

  qa_tt_point pts[4] = {{0, 0}, {0.5, 0}, {0.5, 1}, {0, 1}};
  qa_flattened_contour contour = {pts, 4};
  qa_rasterized_glyph glyph;
  CHECK(qa_truetype_rasterize_contours(&contour, 1, 1, 1, 4, &glyph));
  CHECK(glyph.coverage_length == 1);
  CHECK(glyph.coverage[0] == 128);
  qa_rasterized_glyph_destroy(&glyph);
}

static bool contour_is(qa_tt_contours contours, size_t count,
                       const double (*pts)[2], size_t n) {
  if (contours.count != count) return false;
  if (count == 0) return true;
  if (contours.contours[0].count != n) return false;
  for (size_t i = 0; i < n; i++) {
    if (contours.contours[0].points[i].x != pts[i][0] ||
        contours.contours[0].points[i].y != pts[i][1]) {
      return false;
    }
  }
  return true;
}

static void test_synthetic_font(void) {
  qa_parse_font_result r = qa_truetype_parse_font(k_font, sizeof k_font);
  CHECK(r.ok);
  if (!r.ok) return;
  qa_parsed_font *font = r.font;
  CHECK(qa_parsed_font_units_per_em(font) == 1000);
  CHECK(qa_parsed_font_num_glyphs(font) == 3);
  CHECK(qa_parsed_font_ascent(font) == 800);
  CHECK(qa_parsed_font_descent(font) == -200);
  CHECK(qa_parsed_font_line_gap(font) == 100);
  CHECK(qa_parsed_font_outline_format(font) == QA_OUTLINE_GLYF);
  CHECK(strcmp(qa_parsed_font_outline_format_name(font), "glyf") == 0);

  CHECK(qa_parsed_font_cmap_lookup(font, 0x41) == 1);
  CHECK(qa_parsed_font_cmap_lookup(font, 0x42) == 2);
  CHECK(qa_parsed_font_cmap_lookup(font, 0x43) == 0);
  CHECK(qa_parsed_font_advance_width(font, 0) == 500);
  CHECK(qa_parsed_font_advance_width(font, 1) == 600);
  CHECK(qa_parsed_font_advance_width(font, 2) == 600);
  CHECK(qa_parsed_font_advance_width(font, 9) == 0);
  CHECK(qa_parsed_font_advance_width(font, -1) == 0);

  static const double tri1[3][2] = {{0, 0}, {500, 0}, {0, 700}};
  static const double tri2[3][2] = {{100, 50}, {600, 50}, {100, 750}};
  CHECK(contour_is(qa_parsed_font_contours(font, 1), 1, tri1, 3));
  /* Composite glyph: same triangle translated by (100, 50). */
  CHECK(contour_is(qa_parsed_font_contours(font, 2), 1, tri2, 3));
  CHECK(qa_parsed_font_contours(font, 0).count == 0);
  CHECK(qa_parsed_font_contours(font, 99).count == 0);

  CHECK(qa_parsed_font_kerning(font, 1, 2) == -50);
  CHECK(qa_parsed_font_kerning(font, 2, 1) == 0);

  qa_color_layer *layers = NULL;
  size_t layer_count = 0;
  CHECK(qa_parsed_font_color_layers(font, 2, &layers, &layer_count));
  CHECK(layer_count == 2);
  if (layer_count == 2) {
    CHECK(layers[0].gid == 1 && layers[0].palette_index == 0);
    CHECK(layers[1].gid == 1 && layers[1].palette_index == 0xffff);
  }
  free(layers);
  layers = NULL;
  CHECK(!qa_parsed_font_color_layers(font, 1, &layers, &layer_count));

  qa_tt_color color;
  CHECK(qa_parsed_font_palette_color(font, 0, 0, &color));
  CHECK(color.r == 30 && color.g == 20 && color.b == 10 && color.a == 255);
  CHECK(!qa_parsed_font_palette_color(font, 1, 0, &color));
  CHECK(!qa_parsed_font_palette_color(font, 0, 5, &color));

  qa_rasterized_glyph glyph;
  CHECK(qa_truetype_rasterize_glyph(font, 1, 24, &glyph));
  CHECK(glyph.width == 14 && glyph.height == 19);
  CHECK(glyph.coverage_length == sizeof k_glyph_coverage);
  CHECK(memcmp(glyph.coverage, k_glyph_coverage, sizeof k_glyph_coverage) == 0);
  qa_rasterized_glyph_destroy(&glyph);

  qa_rasterized_glyph empty;
  CHECK(qa_truetype_rasterize_glyph(font, 0, 24, &empty));
  bool any = false;
  for (size_t i = 0; i < empty.coverage_length; i++) {
    if (empty.coverage[i] > 0) any = true;
  }
  CHECK(!any);
  qa_rasterized_glyph_destroy(&empty);

  bool oom = false;
  qa_rasterized_color_glyph color_glyph;
  CHECK(qa_truetype_rasterize_color_glyph(font, 2, 24, 0, NULL, &color_glyph,
                                          &oom));
  CHECK(color_glyph.width == 14 && color_glyph.height == 19);
  CHECK(color_glyph.pixels_length == 14u * 19u * 4u);
  int distinct = 0;
  for (size_t i = 0; i < color_glyph.pixels_length; i += 4) {
    if (color_glyph.pixels[i + 3] == 0) continue;
    bool seen = false;
    for (size_t j = 0; j < i; j += 4) {
      if (color_glyph.pixels[j + 3] > 0 &&
          memcmp(color_glyph.pixels + j, color_glyph.pixels + i, 3) == 0) {
        seen = true;
      }
    }
    if (!seen) distinct += 1;
  }
  CHECK(distinct > 1);
  qa_rasterized_color_glyph_destroy(&color_glyph);
  CHECK(!qa_truetype_rasterize_color_glyph(font, 1, 24, 0, NULL, &color_glyph,
                                           &oom));
  CHECK(!oom);

  int64_t cps[2] = {0x41, 0x42};
  qa_font_atlas atlas;
  CHECK(qa_truetype_build_font_atlas(font, cps, 2, 24, 8, &atlas));
  CHECK(atlas.width == 16 && atlas.height == 41);
  CHECK(atlas.line_height == 19);
  CHECK(atlas.glyph_count == 2);
  CHECK(atlas.pixels_length == (size_t)(atlas.width * atlas.height) * 4u);
  qa_atlas_rect rect;
  CHECK(qa_font_atlas_rect_for(&atlas, 0x41, &rect));
  CHECK(rect.x == 1 && rect.y == 1 && rect.w == 14 && rect.h == 19 && !rect.color);
  CHECK(rect.x + rect.w < atlas.width);
  CHECK(rect.y + rect.h < atlas.height);
  CHECK(qa_font_atlas_rect_for(&atlas, 0x42, &rect));
  CHECK(rect.x == 1 && rect.y == 21 && rect.w == 14 && rect.h == 19 && rect.color);
  CHECK(!qa_font_atlas_rect_for(&atlas, 0x43, &rect));
  qa_font_atlas_destroy(&atlas);

  qa_font_atlas bad;
  CHECK_ABORTS(qa_truetype_build_font_atlas(font, cps, 2, 0, 512, &bad));
  CHECK_ABORTS(qa_truetype_build_font_atlas(font, cps, 2, 24, 0, &bad));

  qa_parse_font_result_destroy(&r);
}

static void test_latin1(void) {
  size_t count = 0;
  int64_t *cps = qa_truetype_latin1_codepoints(&count);
  CHECK(cps != NULL);
  CHECK(count == 191);
  if (cps != NULL && count == 191) {
    CHECK(cps[0] == 0x20);
    CHECK(cps[94] == 0x7e);
    CHECK(cps[95] == 0xa0);
    CHECK(cps[190] == 0xff);
  }
  free(cps);
}

int main(void) {
  test_boundaries();
  test_synthetic_font();
  test_latin1();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
