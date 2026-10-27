/* Port of src/platform/runtime.ts. GPL-2.0-or-later. */
#include "qa/platform/runtime.h"

#include <string.h>

void qa_dedicated_platform_init(qa_dedicated_platform *platform) {
  platform->stopped = false;
}

bool qa_dedicated_platform_closed(const qa_dedicated_platform *platform) {
  return platform->stopped;
}

void qa_dedicated_platform_close(qa_dedicated_platform *platform) {
  platform->stopped = true;
}

void qa_graphical_platform_init(qa_graphical_platform *platform, qa_sdl_window *window) {
  qa_sdl_window_move(&platform->window, window);
}

bool qa_graphical_platform_closed(qa_graphical_platform *platform) {
  return qa_sdl_window_closed(&platform->window);
}

void qa_graphical_platform_close(qa_graphical_platform *platform) {
  qa_sdl_window_close(&platform->window);
}

/** Dedicated startup never initializes SDL. */
bool qa_open_platform(const qa_platform_options *options, qa_platform *out) {
  memset(out, 0, sizeof *out);
  out->kind = options->kind;
  switch (options->kind) {
    case QA_PLATFORM_DEDICATED:
      qa_dedicated_platform_init(&out->as.dedicated);
      return true;
    case QA_PLATFORM_GRAPHICAL:
      return qa_sdl_window_open(&options->window, &out->as.graphical.window);
  }
  return true;
}

bool qa_platform_closed(qa_platform *platform) {
  return platform->kind == QA_PLATFORM_DEDICATED
             ? qa_dedicated_platform_closed(&platform->as.dedicated)
             : qa_graphical_platform_closed(&platform->as.graphical);
}

void qa_platform_close(qa_platform *platform) {
  if (platform->kind == QA_PLATFORM_DEDICATED) {
    qa_dedicated_platform_close(&platform->as.dedicated);
  } else {
    qa_graphical_platform_close(&platform->as.graphical);
  }
}
