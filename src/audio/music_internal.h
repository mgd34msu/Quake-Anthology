#ifndef QA_AUDIO_MUSIC_INTERNAL_H
#define QA_AUDIO_MUSIC_INTERNAL_H
#include "qa/audio.h"

/* The serialized engine has already admitted a finite nonnegative target. */
void qa_audio_music_target_publish(qa_audio_music *, float);
#endif
