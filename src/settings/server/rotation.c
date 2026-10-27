/* Port of src/settings/server/rotation.ts. GPL-2.0-or-later. */
#include "qa/settings/server/rotation.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/settings/server/common.h"

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static qa_server_setting_definition classic_definitions[1];
static qa_server_setting_definition rerelease_definitions[2];
static bool built = false;

static qa_server_setting_definition rotation_definition(bool rerelease) {
  qa_server_setting_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.id = "server:map-rotation";
  definition.label = "Map rotation";
  definition.description =
      "Ordered map names. An empty list follows each map's authored exit.";
  definition.apply_at = QA_SERVER_APPLY_LIVE;
  definition.default_value = "";
  definition.target.is_bit = false;
  definition.target.name = rerelease ? "g_map_list" : "sv_maplist";
  definition.kind = QA_SERVER_SETTING_TEXT_ENTRY;
  definition.maximum_length = 2048;
  return definition;
}

static void build(void) {
  if (built) return;
  built = true;
  classic_definitions[0] = rotation_definition(false);
  rerelease_definitions[0] = rotation_definition(true);
  qa_server_setting_target shuffle = {.is_bit = false, .name = "g_map_list_shuffle"};
  /* The TS spreads `...(rerelease ? [toggle] : [])`: [rotation] for
   * classic, [rotation, shuffle] for rerelease. */
  rerelease_definitions[1] = qa_server_toggle(
      "server:map-rotation-shuffle", shuffle, "Shuffle after last map", false,
      "Reshuffle at the end of the rotation and keep the new order for the "
      "next cycle.",
      QA_SERVER_APPLY_LIVE);
}

qa_server_setting_collection qa_q2_rotation_settings(bool rerelease) {
  build();
  qa_server_setting_collection result;
  result.id = "q2:rotation";
  result.definitions = rerelease ? rerelease_definitions : classic_definitions;
  result.definition_count = rerelease ? 2 : 1;
  return result;
}

char *qa_rotation_map_name(const char *value) {
  /* String.trim, then strip a leading "maps/" and a trailing ".bsp". */
  const char *start = value;
  while (*start != '\0' && isspace((unsigned char)*start)) start++;
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  size_t length = (size_t)(end - start);
  char *name = (char *)malloc(length + 1);
  if (name == NULL) return NULL;
  memcpy(name, start, length);
  name[length] = '\0';
  if (strncmp(name, "maps/", 5) == 0) memmove(name, name + 5, strlen(name + 5) + 1);
  size_t name_length = strlen(name);
  if (name_length >= 4) {
    const char *suffix = name + name_length - 4;
    if (suffix[0] == '.' && tolower((unsigned char)suffix[1]) == 'b' &&
        tolower((unsigned char)suffix[2]) == 's' &&
        tolower((unsigned char)suffix[3]) == 'p')
      name[name_length - 4] = '\0';
  }
  /* /^[a-zA-Z0-9_-]+(?:\/[a-zA-Z0-9_-]+)*$/ over the stripped name. */
  bool valid = name[0] != '\0' && strlen(name) <= 127;
  bool segment = false;
  for (size_t i = 0; valid && name[i] != '\0'; i++) {
    char c = name[i];
    if (c == '/') {
      if (!segment) valid = false;
      segment = false;
    } else if (isalnum((unsigned char)c) || c == '_' || c == '-') {
      segment = true;
    } else {
      valid = false;
    }
  }
  if (valid && !segment) valid = false;
  if (!valid) {
    free(name);
    qa_error("Enter a map name, such as q2dm1 or q64/outpost");
  }
  return name;
}

const char **qa_move_rotation_map(const char *const *maps, size_t count,
                                  double index, int direction) {
  double destination = index + (double)direction;
  if (!(index == trunc(index)) || index < 0 || destination < 0 ||
      index >= (double)count || destination >= (double)count)
    return NULL;
  const char **result = (const char **)malloc(count * sizeof *result);
  if (result == NULL) return NULL;
  for (size_t i = 0; i < count; i++) result[i] = maps[i];
  size_t from = (size_t)index;
  size_t to = (size_t)destination;
  const char *selected = result[from];
  result[from] = result[to];
  result[to] = selected;
  return result;
}
