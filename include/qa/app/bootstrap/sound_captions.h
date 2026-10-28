/* Port of src/app/bootstrap/sound-captions.ts over landed audio/text
 * types. LoadedApplicationContent.forContent (content.ts, sibling
 * slice) is a host fn returning a localization read fn for a content
 * key. Async prepare -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SOUND_CAPTIONS_H
#define QA_APP_BOOTSTRAP_SOUND_CAPTIONS_H
#include <stddef.h>
#include "qa/audio/engine.h"
#include "qa/audio/types.h"
#include "qa/contracts/identity.h"
#include "qa/text/captions.h"
typedef struct qa_app_sound_caption_content {
  void *ctx;
  /* TS reference.provenance.mount.identity.content for a sound reference. */
  const char *(*content_key)(void *ctx, const qa_resolved_resource_reference *reference);
  /* TS content.forContent(key) then mounts.open(path)?.bytes. */
  uint8_t *(*open_bytes)(void *ctx, const char *content_key, const char *path, size_t *out_length);
} qa_app_sound_caption_content;
typedef struct qa_app_seat_sound_captions qa_app_seat_sound_captions;
qa_app_seat_sound_captions *qa_app_seat_sound_captions_create(qa_seat_id seat, qa_unified_audio *audio,
                                                              const char *(*language)(void *ctx), void *language_ctx);
void qa_app_seat_sound_captions_destroy(qa_app_seat_sound_captions *c);
void qa_app_seat_sound_captions_prepare(qa_app_seat_sound_captions *c, qa_app_sound_caption_content content);
/* Malloc'd caption array; free with qa_active_captions_free (text). */
qa_active_caption *qa_app_seat_sound_captions_active(qa_app_seat_sound_captions *c, qa_caption_preferences preferences, size_t *out_count);
void qa_app_seat_sound_captions_close(qa_app_seat_sound_captions *c);
#endif
