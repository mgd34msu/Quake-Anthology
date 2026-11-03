/* Port of src/app/bootstrap/weapon-hud.ts: ApplicationWeaponHudAssets over
 * ApplicationAssets (providers, image registry, catalog). Async -> synchronous;
 * TS throws are qa_raise("Error"). A failed load caches nothing, as TS
 * cacheEvictingRejection evicts the rejected promise.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_HUD_H
#define QA_APP_BOOTSTRAP_WEAPON_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/contracts/content.h"
#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
typedef struct qa_app_weapon_hud_assets qa_app_weapon_hud_assets;
/* TS new ApplicationWeaponHudAssets(assets): the assets are borrowed. */
qa_app_weapon_hud_assets *qa_app_weapon_hud_assets_create(qa_app_application_assets *assets);
void qa_app_weapon_hud_assets_destroy(qa_app_weapon_hud_assets *a);
/* Borrowed picture or NULL (TS undefined); valid until the next load or publish. */
const qa_picture_asset *qa_app_weapon_hud_assets_picture(qa_app_weapon_hud_assets *a, qa_resource_id id);
/* TS prepareImageRefresh(providers): providers is the PreparedApplicationImages
 * (Pick<ApplicationAssets, "provider">); the returned handle is the publish closure. */
typedef struct qa_app_weapon_hud_refresh qa_app_weapon_hud_refresh;
qa_app_weapon_hud_refresh *qa_app_weapon_hud_assets_prepare_image_refresh(qa_app_weapon_hud_assets *a,
                                                                          qa_app_prepared_images *providers);
/* Runs the closure and frees the handle. */
void qa_app_weapon_hud_refresh_publish(qa_app_weapon_hud_refresh *refresh);
/* C-only: drops an unpublished refresh. */
void qa_app_weapon_hud_refresh_discard(qa_app_weapon_hud_refresh *refresh);
/* TS aspect(id): id NULL is TS null. */
double qa_app_weapon_hud_assets_aspect(qa_app_weapon_hud_assets *a, const qa_resource_id *id);
/* TS prepare(status): has_* false is TS null; ids are owned by the assets. */
typedef struct qa_app_weapon_hud_prepared { qa_resource_id weapon; qa_resource_id ammo; bool has_weapon, has_ammo; } qa_app_weapon_hud_prepared;
qa_app_weapon_hud_prepared qa_app_weapon_hud_assets_prepare(qa_app_weapon_hud_assets *a, const qa_weapon_hud_status *status);
/* TS load(icon): the id is owned by the assets (lives until destroy). */
qa_resource_id qa_app_weapon_hud_assets_load(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon);
/* TS readonly assets. */
qa_app_application_assets *qa_app_weapon_hud_assets_assets(qa_app_weapon_hud_assets *a);
#endif
