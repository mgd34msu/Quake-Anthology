#ifndef QA_FRONTEND_RECEIVED_MUSIC_H
#define QA_FRONTEND_RECEIVED_MUSIC_H
#include "music_sources.h"
#include "qa/source_save.h"
typedef struct frontend_received_music frontend_received_music;
bool frontend_received_music_create(qa_frontend *,const frontend_music_origin *,frontend_received_music **,qa_error *);
bool frontend_received_music_play(frontend_received_music *,const char *,qa_error *);
bool frontend_received_music_pause(frontend_received_music *,bool,qa_error *);
bool frontend_received_music_selected(const frontend_received_music *);
bool frontend_received_music_idle(const frontend_received_music *);
bool frontend_received_music_destroy(frontend_received_music **,qa_error *);
bool frontend_received_music_fields(qa_frontend *,frontend_received_music **,qa_source_save_io *,const qa_audio_checkpoint_refs *,qa_error *);
bool frontend_received_music_restore_finish(frontend_received_music *,const frontend_music_origin *,qa_error *);
qa_audio_music *frontend_received_music_player(const frontend_received_music *);
#endif
