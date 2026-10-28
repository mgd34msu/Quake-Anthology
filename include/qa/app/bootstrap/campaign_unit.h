/* Port of src/app/bootstrap/campaign-unit.ts. The codec is a host
 * seam kept for tests; a codec with NULL encode/decode installs
 * the real save-image codec (persistence/save_image.h), as the TS
 * calls encode/decodeSaveImage directly. The unit state machine,
 * entry validation and error strings are kept here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CAMPAIGN_UNIT_H
#define QA_APP_BOOTSTRAP_CAMPAIGN_UNIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/session.h"
/* TS ResourceRequest. Strings borrowed unless owned by the unit. */
typedef struct qa_app_resource_request { qa_content_id content; const char *path; } qa_app_resource_request;
typedef struct qa_app_campaign_codec {
  void *ctx;
  /* Malloc'd encoded image; false only on OOM. */
  bool (*encode)(void *ctx, const qa_save_image *image, uint8_t **out_bytes, size_t *out_count);
  /* Borrowed decoded view valid until the next decode call. */
  const qa_save_image *(*decode)(void *ctx, const uint8_t *bytes, size_t count);
} qa_app_campaign_codec;
typedef struct qa_app_campaign_unit qa_app_campaign_unit;
typedef struct qa_app_campaign_unit_visit qa_app_campaign_unit_visit;
qa_app_campaign_unit *qa_app_campaign_unit_create(qa_app_campaign_codec codec);
void qa_app_campaign_unit_destroy(qa_app_campaign_unit *unit);
/* TS stage(): departure NULL = TS null. Visit owns staged worlds. */
qa_app_campaign_unit_visit *qa_app_campaign_unit_stage(qa_app_campaign_unit *unit, qa_app_resource_request destination,
                                                       bool new_unit, const qa_save_image *departure);
/* Borrowed decoded restore image (codec-owned) or NULL. */
const qa_save_image *qa_app_campaign_unit_visit_restore(const qa_app_campaign_unit_visit *visit);
/* Aborts "Campaign visit was superseded" on stale revision. Frees visit on success. */
void qa_app_campaign_unit_visit_commit(qa_app_campaign_unit_visit *visit);
void qa_app_campaign_unit_visit_destroy(qa_app_campaign_unit_visit *visit);
/* TS checkpoint(): has_current false mirrors the TS default this.current.
 * checkpoint bytes owned via *out_bytes. */
qa_provider_checkpoint qa_app_campaign_unit_checkpoint(qa_app_campaign_unit *unit, bool has_current,
                                                       qa_app_resource_request current, uint8_t **out_bytes, size_t *out_length);
/* TS attach(): fills the appended checkpoint; caller builds the new image. */
qa_provider_checkpoint qa_app_campaign_unit_attach(qa_app_campaign_unit *unit, const qa_save_image *image,
                                                   uint8_t **out_bytes, size_t *out_length);
/* TS worldOnly validation count used by attach/stage is internal. */
void qa_app_campaign_unit_restore(qa_app_campaign_unit *unit, const qa_save_image *image);
#endif
