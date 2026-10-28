/* Port of src/app/bootstrap/splash.ts. Bun childProcess/splash spawn
 * has no C counterpart: run/open become host fn+ctx over SDL (platform
 * linked directly by caller); scaleCover is pure. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SPLASH_H
#define QA_APP_BOOTSTRAP_SPLASH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/formats/images/png.h"
typedef struct qa_app_splash_screen qa_app_splash_screen;
/* Host implements window/process plumbing. open returns false for TS null. */
typedef struct qa_app_splash_host {
  void *ctx;
  bool (*open_window)(void *ctx, double width, double height, const uint8_t *rgba, size_t length);
  bool (*poll_quit)(void *ctx);       /* true when user closed / parent gone */
  void (*present)(void *ctx);
  void (*close_window)(void *ctx);
  void (*sleep_ms)(void *ctx, double ms);
  void (*write_stdout)(void *ctx, const char *text);
  double (*now_ms)(void *ctx);
  bool (*load_title_image)(void *ctx, qa_png_image *out);
} qa_app_splash_host;
uint8_t *qa_app_scale_cover(const qa_png_image *image, double width, double height, size_t *out_length);
int qa_app_run_splash_child(qa_app_splash_host host, double width, double height);
qa_app_splash_screen *qa_app_open_splash(qa_app_splash_host host, double width, double height); /* NULL = TS null */
void qa_app_splash_screen_close(qa_app_splash_screen *s);
#endif
