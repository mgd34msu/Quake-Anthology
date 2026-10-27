/* Port of src/render/scene/image-policy.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_IMAGE_POLICY_H
#define QA_RENDER_SCENE_IMAGE_POLICY_H
#include <stdbool.h>
#include <stddef.h>
/* TS ImageUsage / ImageFormat exact strings. */
typedef const char *qa_scene_image_usage;
typedef const char *qa_scene_image_format;
typedef struct qa_scene_image_policy {
  double override_level;
  const qa_scene_image_usage *override_usages; size_t override_usage_count;
  bool has_formats; /* false is TS undefined */
  const qa_scene_image_format *formats; size_t format_count;
} qa_scene_image_policy;
/* Owned snapshot storage; destroy frees arrays (strings are literals). */
typedef struct qa_scene_image_policy_owned {
  qa_scene_image_policy policy;
  qa_scene_image_usage *usages; qa_scene_image_format *formats;
} qa_scene_image_policy_owned;
void qa_scene_image_policy_owned_destroy(qa_scene_image_policy_owned *owned);
/* Malloc'd literal-string array; caller frees array. */
const qa_scene_image_format *qa_parse_image_formats(const char *value, size_t *out_count);
bool qa_snapshot_image_policy(qa_scene_image_policy policy, qa_scene_image_policy_owned *out);
typedef struct qa_scene_image_controls { double override_level, override_mask; const char *formats; } qa_scene_image_controls;
bool qa_image_policy_from_controls(qa_scene_image_controls controls, qa_scene_image_policy_owned *out);
/* TS DEFAULT_IMAGE_POLICY (overrideLevel 1, mask -1, all formats). */
qa_scene_image_policy qa_default_image_policy(void);
#endif
