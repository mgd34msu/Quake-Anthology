/* Port of src/app/bootstrap/sound-captions.ts: SeatSoundCaptions follows the
 * seat's mixer voices and reads source-authored caption sidecars through
 * LoadedApplicationContent.forContent. Async prepare -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SOUND_CAPTIONS_H
#define QA_APP_BOOTSTRAP_SOUND_CAPTIONS_H
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/audio/engine.h"
#include "qa/audio/types.h"
#include "qa/contracts/identity.h"
#include "qa/text/captions.h"
typedef struct qa_app_seat_sound_captions qa_app_seat_sound_captions;
/* TS new SeatSoundCaptions(seat, audio, language, warn = () => {}): warn NULL is the default. */
qa_app_seat_sound_captions *qa_app_seat_sound_captions_create(qa_seat_id seat, qa_unified_audio *audio,
    const char *(*language)(void *ctx), void *language_ctx, void (*warn)(void *ctx, const char *text), void *warn_ctx);
void qa_app_seat_sound_captions_destroy(qa_app_seat_sound_captions *c);
/* TS prepare(content): content is Pick<LoadedApplicationContent, "forContent">. */
void qa_app_seat_sound_captions_prepare(qa_app_seat_sound_captions *c, qa_app_loaded_content *content);
/* Malloc'd caption array; free with qa_active_captions_free (text). */
qa_active_caption *qa_app_seat_sound_captions_active(qa_app_seat_sound_captions *c, qa_caption_preferences preferences, size_t *out_count);
void qa_app_seat_sound_captions_close(qa_app_seat_sound_captions *c);
#endif
