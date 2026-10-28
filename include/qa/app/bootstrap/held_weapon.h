/* Port of src/app/bootstrap/held-weapon.ts. Declaration/model
 * loading and attachment alignment are landed/host services; cache
 * keys, qualification guards and pass shape are kept here.
 * Named qa_app_foreign_* after the landed native_held_weapon peer.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_HELD_WEAPON_H
#define QA_APP_BOOTSTRAP_HELD_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/held_weapon.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
/* SimulationPresentation fields read here. */
typedef struct qa_app_held_source {
  bool has_held_weapon; qa_held_weapon_declaration held_weapon;
  const char *path;
  qa_content_id content;
  const char *family; /* "q1"|"q2"|other */
  const char *weapon_item;
  qa_actor_id actor;
} qa_app_held_source;
/* One held character pass (SceneEntity fields flattened for the host). */
typedef struct qa_app_held_pass {
  qa_content_id content;
  qa_actor_id actor;
  const void *resource, *model;
  double frame, opacity;
  qa_vec4 color;
  qa_vec3 lighting_origin;
} qa_app_held_pass;
typedef struct qa_app_foreign_held_weapons qa_app_foreign_held_weapons;
typedef struct qa_app_held_host {
  void *ctx;
  /* Read <path>.held.json; false = TS undefined. */
  bool (*read_declaration)(void *ctx, qa_content_id content, const char *held_path, qa_held_weapon_declaration *out);
  bool (*resource_exists)(void *ctx, qa_content_id content, const char *path);
  /* Load a model asset; kind text + frame count + digest filled. */
  const char *(*load_model)(void *ctx, qa_content_id content, const char *path, const void **resource, const void **model,
                            const char **kind, const char **digest, size_t *frame_count);
  /* q1/q2 authored held fallbacks; false = TS null. */
  bool (*fallback_held)(void *ctx, const char *family, const char *path, const char *weapon_item, qa_held_weapon_model *out);
} qa_app_held_host;
qa_app_foreign_held_weapons *qa_app_foreign_held_weapons_create(qa_app_held_host host);
void qa_app_foreign_held_weapons_destroy(qa_app_foreign_held_weapons *weapons);
/* False = TS undefined declaration. */
bool qa_app_foreign_held_weapons_declaration(qa_app_foreign_held_weapons *weapons, qa_app_held_source source, qa_held_weapon_declaration *out);
/* Returns pass count (0 or 1); aborts with exact TS Errors. */
size_t qa_app_foreign_held_weapons_frame(qa_app_foreign_held_weapons *weapons, qa_app_held_source source,
                                         qa_vec3 origin, qa_vec4 color, bool has_opacity, double opacity, qa_app_held_pass *out);
#endif
