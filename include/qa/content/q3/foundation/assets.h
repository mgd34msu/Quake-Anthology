/* Port of src/content/q3/foundation/assets.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_ASSETS_H
#define QA_CONTENT_Q3_FOUNDATION_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/scene.h"

typedef struct qa_q3_character_selection {
  const char *model, *skin, *head_model, *head_skin;
  const char *team; /* NULL | "red" | "blue" */
  const char *team_name;
} qa_q3_character_selection;
/* decoded/scene own the parsed storage; model aliases scene.model. */
typedef struct qa_q3_character_part {
  qa_resolved_resource_reference resource;
  qa_q3_mesh_model model;
  qa_decoded_md3_model decoded; qa_decoded_scene_md3 scene;
  qa_resolved_resource_reference skin_resource;
  qa_skin_surface *surfaces; size_t surface_count;
} qa_q3_character_part;
typedef struct qa_q3_character_assets {
  qa_q3_character_selection selection;
  qa_q3_character_part lower, upper, head;
  qa_resolved_resource_reference animation_resource;
  qa_q3_player_animation_config animation;
  bool has_icon; qa_resolved_resource_reference icon;
} qa_q3_character_assets;
/* TS OpenedResource: bytes are malloc'd and transferred to the loader. */
typedef struct qa_q3_character_opened_resource {
  qa_resolved_resource_reference reference; uint8_t *bytes; size_t byte_length;
} qa_q3_character_opened_resource;
typedef struct qa_q3_character_resources {
  void *ctx;
  bool (*open)(void *ctx, const char *path, qa_q3_character_opened_resource *out); /* false = TS null */
} qa_q3_character_resources;
/* Errors abort with TS messages. Free with qa_q3_character_assets_destroy. */
void qa_q3_load_character(qa_q3_character_resources resources, qa_q3_character_selection selection,
    qa_q3_character_assets *out);
void qa_q3_character_assets_destroy(qa_q3_character_assets *assets);
#endif
