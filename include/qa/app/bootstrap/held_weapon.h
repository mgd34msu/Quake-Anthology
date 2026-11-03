/* Port of src/app/bootstrap/held-weapon.ts (ForeignHeldWeapons): authored
 * held-weapon declarations (`<path>.held.json`), the Q1/Q2 authored
 * fallbacks and the selected held model, aligned to the Q3 hand grip.
 * Named qa_app_foreign_* after the TS class.
 *
 * C accommodations:
 *  - Async loads are synchronous. The TS promise caches keep settled
 *    values only (cacheEvictingRejection): a failure raises through
 *    qa_raise and leaves no entry.
 *  - Declarations read from a file and the Q1 part-filtered model copies
 *    are owned by the weapons until destroy; every other pointer a pass
 *    carries is borrowed from the assets (ApplicationAssets keeps its
 *    models until it closes) or from the source presentation.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_HELD_WEAPON_H
#define QA_APP_BOOTSTRAP_HELD_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/mounts.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/content.h"
#include "qa/contracts/held_weapon.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

/* TS HeldAssets: { provider(content): {mounts}, model(content, path) }.
 * ApplicationAssets is one (qa_app_held_assets_application). mounts returns
 * the provider's borrowed mounts; model raises as ApplicationAssets.model
 * does and only its resource and model are read. */
typedef struct qa_app_held_assets {
  void *ctx;
  qa_content_mounted *(*mounts)(void *ctx, qa_content_id content);
  qa_app_model_asset (*model)(void *ctx, qa_content_id content, const char *path);
} qa_app_held_assets;
/* The HeldAssets view of ApplicationAssets (borrowed; valid while it is open). */
qa_app_held_assets qa_app_held_assets_application(qa_app_application_assets *assets);

/* TS Pick<Q3CharacterView, "origin" | "color" | "opacity">. */
typedef struct qa_app_held_character {
  qa_vec3 origin;
  qa_vec4 color;
  bool has_opacity; double opacity; /* false is TS undefined */
} qa_app_held_character;

typedef struct qa_app_foreign_held_weapons qa_app_foreign_held_weapons;
/* NULL only on allocation failure. */
qa_app_foreign_held_weapons *qa_app_foreign_held_weapons_create(qa_app_held_assets assets);
void qa_app_foreign_held_weapons_destroy(qa_app_foreign_held_weapons *weapons);
/* TS declaration(source): NULL is TS undefined. The result is the source's own
 * declaration or one cached by the weapons. Raises like the TS rejection. */
const qa_held_weapon_declaration *qa_app_foreign_held_weapons_declaration(qa_app_foreign_held_weapons *weapons,
    const qa_app_simulation_presentation *source);
/* TS frame(source, character): writes 0 or 1 passes into out and returns the
 * count. The entity's actor points at source->actor; its resource and model are
 * the assets' (or the weapons' part copy). Raises the TS Error texts. */
size_t qa_app_foreign_held_weapons_frame(qa_app_foreign_held_weapons *weapons,
    const qa_app_simulation_presentation *source, qa_app_held_character character, qa_q3_character_pass *out);
#endif
