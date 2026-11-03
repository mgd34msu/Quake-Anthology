/* Port of src/app/bootstrap/native-held-weapon.ts (NativeHeldWeapons): a
 * native carrier model (a Q2 player body) holds the selected weapon through
 * its authored attachment (`<model>.attachment.json`, else the built-in Q2
 * weapon attachments), sampled per frame on the carrier or its enhanced
 * replacement.
 *
 * C accommodations:
 *  - Async loads are synchronous. The TS grip cache keeps every settled
 *    promise, rejections included: a grip whose preparation raised raises
 *    the same error again on every later request for its key.
 *  - attachment() returns a value; its child closure becomes
 *    qa_app_native_held_attachment_bind, whose result owns the inherited
 *    child tree and is the TS ResolvedHeldEntity.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NATIVE_HELD_WEAPON_H
#define QA_APP_BOOTSTRAP_NATIVE_HELD_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/render/scene/models/grip.h"
#include "qa/render/scene/models/replacements.h"

/* TS NativeHeldAssets: { modelPolicy, provider(content): {mounts} }. The
 * policy is read where TS reads the property (ApplicationAssets: a getter). */
typedef struct qa_app_native_held_assets {
  void *ctx;
  qa_model_replacement_policy (*model_policy)(void *ctx);
  qa_content_mounted *(*mounts)(void *ctx, qa_content_id content);
} qa_app_native_held_assets;
qa_app_native_held_assets qa_app_native_held_assets_application(qa_app_application_assets *assets);

typedef struct qa_app_native_held_weapons qa_app_native_held_weapons;
/* NULL only on allocation failure. */
qa_app_native_held_weapons *qa_app_native_held_weapons_create(qa_app_native_held_assets assets);
void qa_app_native_held_weapons_destroy(qa_app_native_held_weapons *weapons);

/* TS attachment(content, carrier) result. The carrier is a shallow copy: what
 * it points at must outlive the attachment and every entity bound from it.
 * The grips belong to the weapons. */
typedef struct qa_app_native_held_attachment {
  qa_app_native_held_weapons *weapons;
  qa_scene_entity carrier;
  const qa_model_grip *original;
  const qa_model_grip *enhanced; /* NULL is TS null */
} qa_app_native_held_attachment;
/* False is the TS null (a carrier without an attachment definition). Raises
 * the TS errors of grip preparation. */
bool qa_app_native_held_weapons_attachment(qa_app_native_held_weapons *weapons, qa_content_id content,
    const qa_scene_entity *carrier, qa_app_native_held_attachment *out);

/* TS ResolvedHeldEntity = (camera, purpose) => SceneEntity | null. */
typedef struct qa_app_resolved_held_entity qa_app_resolved_held_entity;
/* The attachment's child closure: inherits the carrier's flags, opacity,
 * lighting origin and shadow plane into a copy of child's tree. NULL only on
 * allocation failure. */
qa_app_resolved_held_entity *qa_app_native_held_attachment_bind(const qa_app_native_held_attachment *attachment,
                                                                const qa_scene_entity *child);
void qa_app_resolved_held_entity_destroy(qa_app_resolved_held_entity *resolved);
/* purpose "view" | "shadow". False is the TS null. *out shares the resolved
 * entity's attachment tree. Raises "Native attachment replacement changed
 * after preparation". */
bool qa_app_resolved_held_entity_resolve(const qa_app_resolved_held_entity *resolved, qa_vec3 camera,
                                         const char *purpose, qa_scene_entity *out);
#endif
