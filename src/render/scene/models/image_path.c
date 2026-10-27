/* Port of src/render/scene/models/image-path.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/image_path.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"

char *qa_model_image_path(const char *name) {
  size_t length = strlen(name);
  char *path = malloc(length + 1);
  if (path == NULL) return NULL;
  for (size_t i = 0; i < length; i++) path[i] = name[i] == '\\' ? '/' : name[i];
  path[length] = '\0';
  if (length >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
    free(path);
    qa_range_error("Invalid model image path: %s", name);
  }
  /* Split on '/', folding "." and ".." exactly as the TS loop. */
  char **parts = NULL;
  size_t count = 0, capacity = 0;
  size_t start = 0;
  for (;;) {
    size_t end = start;
    while (end < length && path[end] != '/') end++;
    size_t part_length = end - start;
    if (part_length == 0) {
      for (size_t i = 0; i < count; i++) free(parts[i]);
      free(parts); free(path);
      qa_range_error("Invalid model image path: %s", name);
    }
    if (part_length == 1 && path[start] == '.') {
      /* continue */
    } else if (part_length == 2 && path[start] == '.' && path[start + 1] == '.') {
      if (count == 0) {
        free(parts); free(path);
        qa_range_error("Model image escapes content root: %s", name);
      }
      free(parts[--count]);
    } else {
      if (count == capacity) {
        capacity = capacity ? capacity * 2 : 8;
        parts = realloc(parts, capacity * sizeof *parts);
        if (parts == NULL) { free(path); return NULL; }
      }
      parts[count] = malloc(part_length + 1);
      if (parts[count] == NULL) {
        for (size_t i = 0; i < count; i++) free(parts[i]);
        free(parts); free(path); return NULL;
      }
      memcpy(parts[count], path + start, part_length);
      parts[count][part_length] = '\0';
      count++;
    }
    if (end >= length) break;
    start = end + 1;
  }
  free(path);
  size_t joined_length = 0;
  for (size_t i = 0; i < count; i++) joined_length += strlen(parts[i]) + 1;
  char *joined = malloc(joined_length + 1);
  if (joined == NULL) {
    for (size_t i = 0; i < count; i++) free(parts[i]);
    free(parts);
    return NULL;
  }
  joined[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i != 0) strcat(joined, "/");
    strcat(joined, parts[i]);
    free(parts[i]);
  }
  free(parts);
  char *result = qa_normalize_resource_path(joined);
  free(joined);
  return result;
}
