/* Port of src/app/bootstrap/server-map.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_map.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
char *qa_app_server_map_path(const char *name) {
  char *normalized = qa_normalize_resource_path(name);
  if (normalized == NULL) qa_range_error("Out of memory");
  bool prefixed = strncmp(normalized, "maps/", 5) == 0;
  size_t length = strlen(normalized);
  /* "maps/" cannot complete a ".bsp" suffix, so the normalized name decides it */
  bool suffixed = length >= 4 && strcmp(normalized + length - 4, ".bsp") == 0;
  size_t size = length + (prefixed ? 0 : 5) + (suffixed ? 0 : 4) + 1;
  char *out = malloc(size);
  if (out == NULL) { free(normalized); qa_range_error("Out of memory"); }
  snprintf(out, size, "%s%s%s", prefixed ? "" : "maps/", normalized, suffixed ? "" : ".bsp");
  free(normalized);
  return out;
}
