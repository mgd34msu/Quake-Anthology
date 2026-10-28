/* Port of src/app/bootstrap/held-weapon.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/held_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct { char *key; bool has; qa_held_weapon_declaration declaration; } declaration_row;
struct qa_app_foreign_held_weapons {
  qa_app_held_host host;
  declaration_row *rows; size_t count, cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_foreign_held_weapons *qa_app_foreign_held_weapons_create(qa_app_held_host host) {
  qa_app_foreign_held_weapons *weapons = calloc(1, sizeof *weapons);
  if (!weapons) return NULL;
  weapons->host = host;
  return weapons;
}
void qa_app_foreign_held_weapons_destroy(qa_app_foreign_held_weapons *weapons) {
  if (!weapons) return;
  for (size_t i = 0; i < weapons->count; i++) free(weapons->rows[i].key);
  free(weapons->rows);
  free(weapons);
}
bool qa_app_foreign_held_weapons_declaration(qa_app_foreign_held_weapons *weapons, qa_app_held_source source, qa_held_weapon_declaration *out) {
  if (source.has_held_weapon) { *out = source.held_weapon; return true; }
  if (source.path[0] == '\0') return false;
  char key[4096];
  snprintf(key, sizeof key, "%s/%s", source.content, source.path);
  for (size_t i = 0; i < weapons->count; i++)
    if (strcmp(weapons->rows[i].key, key) == 0) {
      if (weapons->rows[i].has) *out = weapons->rows[i].declaration;
      return weapons->rows[i].has;
    }
  char held_path[4096];
  snprintf(held_path, sizeof held_path, "%s.held.json", source.path);
  qa_held_weapon_declaration declaration = {0};
  bool has = weapons->host.read_declaration(weapons->host.ctx, source.content, held_path, &declaration);
  if (weapons->count == weapons->cap) {
    weapons->cap = weapons->cap ? weapons->cap * 2 : 8;
    weapons->rows = realloc(weapons->rows, weapons->cap * sizeof *weapons->rows);
  }
  weapons->rows[weapons->count++] = (declaration_row){dup(key), has, declaration};
  if (has) *out = declaration;
  return has;
}
size_t qa_app_foreign_held_weapons_frame(qa_app_foreign_held_weapons *weapons, qa_app_held_source source,
                                         qa_vec3 origin, qa_vec4 color, bool has_opacity, double opacity, qa_app_held_pass *out) {
  qa_held_weapon_declaration declaration = {0};
  bool has_declaration = qa_app_foreign_held_weapons_declaration(weapons, source, &declaration);
  if (has_declaration && !declaration.is_model) return 0;
  qa_held_weapon_model held;
  bool has_held = has_declaration && declaration.is_model;
  if (has_held) held = declaration.model;
  else if (strcmp(source.family, "q2") == 0 || strcmp(source.family, "q1") == 0)
    has_held = weapons->host.fallback_held(weapons->host.ctx, source.family, source.path, source.weapon_item, &held);
  if (!has_held)
    qa_range_error("Selected weapon %s/%s has no authored held model declaration", source.content, source.path);
  const char *path = held.path;
  if (!weapons->host.resource_exists(weapons->host.ctx, source.content, path)) {
    if (!held.fallback || !weapons->host.resource_exists(weapons->host.ctx, source.content, held.fallback))
      qa_range_error("Source held model is absent: %s/%s", source.content, held.path);
    path = held.fallback;
  }
  const void *resource = NULL, *model = NULL;
  const char *kind = NULL, *digest = NULL;
  size_t frame_count = 0;
  const char *error = weapons->host.load_model(weapons->host.ctx, source.content, path, &resource, &model, &kind, &digest, &frame_count);
  if (error) qa_range_error("%s", error);
  if (held.part) {
    bool qualified = false;
    for (size_t i = 0; i < held.part->digests.count; i++)
      if (digest && strcmp(held.part->digests.values[i], digest) == 0) qualified = true;
    if (!qualified || strcmp(kind, "q1-mdl") != 0)
      qa_range_error("Held model subset is not qualified for %s/%s", source.content, path);
    /* Triangle filtering happens in the model host (asset.model copy). */
  }
  if (held.has_digest && (!digest || strcmp(held.digest, digest) != 0))
    qa_range_error("Held model digest differs from its source declaration");
  if (strcmp(kind, "brush-model") == 0 || held.reference_frame >= (double)frame_count)
    qa_range_error("Held model reference frame is outside its source animation");
  out->content = source.content;
  out->actor = source.actor;
  out->resource = resource;
  out->model = model;
  out->frame = held.reference_frame;
  out->color = color;
  out->opacity = has_opacity ? opacity : 1;
  out->lighting_origin = origin;
  return 1;
}
