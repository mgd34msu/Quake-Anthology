/*
 * Port of src/content/catalog/start-maps.ts: mapdb.json authored
 * start parsing. Results own malloc'd strings; free with
 * qa_parse_authored_starts_free. Invalid documents abort with the
 * exact TS Error messages. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_START_MAPS_H
#define QA_CONTENT_CATALOG_START_MAPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"

typedef struct qa_authored_episode {
  char *id;
  char *command;
  char *name;
  char *activity;
  bool needs_skill_select;
} qa_authored_episode;

typedef struct qa_authored_start_map {
  char *episode;
  char *bsp;
  char *path;
  char *title;
  char *start_items;
  bool singleplayer;
  bool cooperative;
  bool capture_the_flag;
} qa_authored_start_map;

typedef struct qa_authored_start_catalog {
  qa_resolved_resource_reference resource;
  qa_authored_episode *episode; /* nullable */
  qa_authored_start_map *starts;
  size_t start_count;
} qa_authored_start_catalog;

/* Pick<AuthoredStartCatalog, "episode" | "starts">. */
typedef struct qa_authored_starts {
  qa_authored_episode *episode; /* nullable */
  qa_authored_start_map *starts;
  size_t start_count;
} qa_authored_starts;

/* TS parseAuthoredStarts. False only on OOM. */
bool qa_parse_authored_starts(const uint8_t *bytes, size_t length,
                              const char *campaign, qa_authored_starts *out);
void qa_authored_starts_free(qa_authored_starts *starts);

#endif
