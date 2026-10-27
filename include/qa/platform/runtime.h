/*
 * Port of src/platform/runtime.ts: dedicated/graphical platform
 * selection. Importing opens no library; dedicated startup never
 * initializes SDL. The TS union return is one tagged struct; the
 * graphical window is owned by the platform and closed with it.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_RUNTIME_H
#define QA_PLATFORM_RUNTIME_H

#include <stdbool.h>

#include "qa/platform/sdl.h"

typedef enum qa_platform_kind {
  QA_PLATFORM_DEDICATED,
  QA_PLATFORM_GRAPHICAL
} qa_platform_kind;

typedef struct qa_platform_options {
  qa_platform_kind kind;
  qa_sdl_window_options window; /* graphical only */
} qa_platform_options;

typedef struct qa_dedicated_platform {
  bool stopped;
} qa_dedicated_platform;

void qa_dedicated_platform_init(qa_dedicated_platform *platform);
bool qa_dedicated_platform_closed(const qa_dedicated_platform *platform);
void qa_dedicated_platform_close(qa_dedicated_platform *platform);

typedef struct qa_graphical_platform {
  qa_sdl_window window;
} qa_graphical_platform;

/* Takes ownership of an already-open window (moved; source emptied). */
void qa_graphical_platform_init(qa_graphical_platform *platform, qa_sdl_window *window);
bool qa_graphical_platform_closed(qa_graphical_platform *platform);
void qa_graphical_platform_close(qa_graphical_platform *platform);

typedef struct qa_platform {
  qa_platform_kind kind;
  union {
    qa_dedicated_platform dedicated;
    qa_graphical_platform graphical;
  } as;
} qa_platform;

/* False only on OOM inside window creation. */
bool qa_open_platform(const qa_platform_options *options, qa_platform *out);
bool qa_platform_closed(qa_platform *platform);
void qa_platform_close(qa_platform *platform);

#endif
