/* Port of src/app/bootstrap/splash.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/splash.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_splash_screen { qa_app_splash_host host; bool closed; };
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
uint8_t *qa_app_scale_cover(const qa_png_image *image, double width, double height, size_t *out_length) {
  double iw = image->level.width, ih = image->level.height;
  const uint8_t *pixels = image->level.pixels.bytes;
  size_t plen = image->level.pixels.length;
  size_t out_len = (size_t)(width * height * 4);
  uint8_t *out = calloc(out_len ? out_len : 1, 1);
  if (!out) qa_range_error("Out of memory");
  double scale = fmax(width / iw, height / ih);
  double source_width = width / scale;
  double origin_x = (iw - source_width) / 2, origin_y = 0;
  for (double y = 0; y < height; y++) {
    double source_y = origin_y + (y + 0.5) / scale - 0.5;
    double y0 = clampd(floor(source_y), 0, ih - 1), y1 = fmin(ih - 1, y0 + 1);
    double fy = clampd(source_y - y0, 0, 1);
    for (double x = 0; x < width; x++) {
      double source_x = origin_x + (x + 0.5) / scale - 0.5;
      double x0 = clampd(floor(source_x), 0, iw - 1), x1 = fmin(iw - 1, x0 + 1);
      double fx = clampd(source_x - x0, 0, 1);
      size_t target = (size_t)((y * width + x) * 4);
      for (int channel = 0; channel < 4; channel++) {
        size_t i00 = (size_t)((y0 * iw + x0) * 4) + (size_t)channel;
        size_t i01 = (size_t)((y0 * iw + x1) * 4) + (size_t)channel;
        size_t i10 = (size_t)((y1 * iw + x0) * 4) + (size_t)channel;
        size_t i11 = (size_t)((y1 * iw + x1) * 4) + (size_t)channel;
        double p00 = i00 < plen ? pixels[i00] : 0, p01 = i01 < plen ? pixels[i01] : 0;
        double p10 = i10 < plen ? pixels[i10] : 0, p11 = i11 < plen ? pixels[i11] : 0;
        double top = p00 * (1 - fx) + p01 * fx;
        double bottom = p10 * (1 - fx) + p11 * fx;
        double v = round(top * (1 - fy) + bottom * fy);
        /* Uint8Array store wraps/clamps via ToIntegerOrInfinity then mod 256. */
        out[target + (size_t)channel] = (uint8_t)v;
      }
      out[target + 3] = 255;
    }
  }
  if (out_length) *out_length = out_len;
  return out;
}
int qa_app_run_splash_child(qa_app_splash_host host, double width, double height) {
  qa_png_image image; memset(&image, 0, sizeof image);
  if (!host.load_title_image(host.ctx, &image)) qa_range_error("Out of memory");
  size_t len = 0; uint8_t *frame = qa_app_scale_cover(&image, width, height, &len);
  if (!host.open_window(host.ctx, width, height, frame, len)) { free(frame); qa_png_image_destroy(&image); qa_range_error("Out of memory"); }
  host.present(host.ctx);
  host.write_stdout(host.ctx, "ready\n");
  long ticks = 0;
  for (;;) {
    if (host.poll_quit(host.ctx)) break;
    if (++ticks % 30 == 0) { char buf[64]; snprintf(buf, sizeof buf, "tick %.0f\n", host.now_ms(host.ctx)); host.write_stdout(host.ctx, buf); }
    host.sleep_ms(host.ctx, 16);
  }
  host.close_window(host.ctx);
  free(frame); qa_png_image_destroy(&image);
  return 0;
}
qa_app_splash_screen *qa_app_open_splash(qa_app_splash_host host, double width, double height) {
  if (getenv("QTS_SPLASH_CHILD") != NULL) return NULL;
  /* Host spawns child/process; failure (false) is TS null, never blocks launch. */
  if (!host.open_window(host.ctx, width, height, NULL, 0)) return NULL;
  qa_app_splash_screen *s = calloc(1, sizeof *s);
  if (!s) { host.close_window(host.ctx); return NULL; }
  s->host = host;
  return s;
}
void qa_app_splash_screen_close(qa_app_splash_screen *s) {
  if (!s || s->closed) return;
  s->closed = true;
  s->host.close_window(s->host.ctx);
  free(s);
}
