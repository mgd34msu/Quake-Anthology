#ifndef QA_AUDIO_MUSIC_ENGINE_H
#define QA_AUDIO_MUSIC_ENGINE_H
#include "qa/audio.h"
#include "qa/audio_music_prepare.h"
typedef enum qa_audio_music_lifetime {
    QA_AUDIO_MUSIC_WORLD, QA_AUDIO_MUSIC_MENU
} qa_audio_music_lifetime;
/* Transfers one player reference with its source-authored round lifetime and
 * actual output selection. MENU survives WORLD round reset; full stop, remove
 * and engine destruction still stop playback and detach that reference. */
bool qa_audio_engine_music_source(qa_audio_engine *, uint64_t id, uint32_t audience,
    float gain, qa_audio_music *, qa_audio_music_lifetime, bool active, qa_error *);
/* The caller publishes a genuine output handoff. Inactive players neither
 * update smoothing nor consume PCM; their manual CD pause remains untouched. */
bool qa_audio_engine_music_output(qa_audio_engine *, uint64_t id,
    const qa_audio_music *, bool active, qa_error *);
/* Pure actual bus/player/route observation, including under a gains lease.
 * It does not admit source ownership or make a pending child ready. */
bool qa_audio_engine_music_source_is(const qa_audio_engine *, uint64_t id,
    const qa_audio_music *, qa_audio_music_lifetime, bool active);
/* Rebind genuine imported external-control players to their retained cold
 * control owner. Already bound rows must name that exact owner. */
bool qa_audio_engine_music_controls_restore_bind(qa_audio_engine *,
    qa_audio_music_controls *, qa_error *);
#endif
