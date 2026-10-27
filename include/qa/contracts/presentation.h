/*
 * Port of src/contracts/presentation.ts: presentation owners and
 * component media requests. Runtime helpers live in presentation.c.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_PRESENTATION_H
#define QA_CONTRACTS_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/identity.h"

typedef struct qa_presentation_owner {
  qa_provider_id provider;
  double generation;
} qa_presentation_owner;

/* TS ComponentPresentationMediaRequest union on kind:
 * "music"(intro,loop) | "music-stop" |
 * "shader-remap"(original,replacement,timeOffset). */
typedef enum qa_component_presentation_media_request_kind {
  QA_MEDIA_REQUEST_MUSIC, QA_MEDIA_REQUEST_MUSIC_STOP, QA_MEDIA_REQUEST_SHADER_REMAP
} qa_component_presentation_media_request_kind;
typedef struct qa_component_presentation_media_request {
  qa_component_presentation_media_request_kind kind;
  const char *intro;
  const char *loop;
  const char *original;
  const char *replacement;
  double time_offset;
} qa_component_presentation_media_request;

/* TS presentationOwnerKey(undefined) is "primary"; otherwise the key
 * is JSON.stringify([provider, generation]). NULL owner = undefined.
 * Returns a malloc'd string (caller frees), NULL only on OOM. */
char *qa_presentation_owner_key(const qa_presentation_owner *owner);
/* Mirrors TS exactly: an undefined left never matches (the
 * left?.provider check is false and short-circuits). */
bool qa_same_presentation_owner(const qa_presentation_owner *left,
                                qa_presentation_owner right);

#endif
