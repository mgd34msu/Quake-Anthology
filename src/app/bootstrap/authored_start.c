/* Port of src/app/bootstrap/authored-start.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/authored_start.h"
#include <stdio.h>
#include <string.h>
#include "qa/content/q2/foundation/start_items.h"
bool qa_app_authored_campaign_start_for(const qa_authored_start_catalog *catalog, const char *selected_map,
                                    qa_app_authored_campaign_start *out) {
  const char *stem = selected_map;
  if (strncmp(stem, "maps/", 5) == 0) stem += 5;
  char normalized[1024];
  size_t len = strlen(stem);
  if (len >= 4 && strcmp(stem + len - 4, ".bsp") == 0) len -= 4;
  snprintf(normalized, sizeof normalized, "maps/%.*s.bsp", (int)len, stem);
  if (!catalog) return false;
  const qa_authored_start_map *start = NULL;
  for (size_t i = 0; i < catalog->start_count; i++)
    if (strcmp(catalog->starts[i].path, normalized) == 0) { start = &catalog->starts[i]; break; }
  if (!start) return false;
  size_t item_count = 0;
  qa_q2_start_item *items = qa_q2_parse_start_items(start->start_items, &item_count);
  qa_q2_start_items_free(items, item_count);
  out->target = qa_app_parse_q2_travel(start->bsp);
  out->start_items = start->start_items;
  return true;
}
