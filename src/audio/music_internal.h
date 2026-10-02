#ifndef QA_AUDIO_MUSIC_INTERNAL_H
#define QA_AUDIO_MUSIC_INTERNAL_H
#include "qa/audio.h"
#include "qa/audio_music_prepare.h"

/* The serialized engine has already admitted a finite nonnegative target. */
void qa_audio_music_target_publish(qa_audio_music *, float);
/* Ordinary target setters admit all actual players before writing any gain. */
bool qa_audio_music_target_mutation_ready(qa_audio_music *, qa_error *);
#endif
