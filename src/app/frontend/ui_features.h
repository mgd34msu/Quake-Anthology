#ifndef QA_FRONTEND_UI_FEATURES_H
#define QA_FRONTEND_UI_FEATURES_H
#include "internal.h"
#include "qa/media_captions.h"
#include "qa/persistence_content.h"
bool frontend_ui_features_prepare(qa_frontend *, qa_error *);
bool frontend_ui_features_destroy(qa_frontend *, qa_error *);
/* Strict retained-child admission, distinct from returned draw callbacks. */
bool frontend_ui_features_idle(const qa_frontend *);
bool frontend_ui_features_sync(qa_frontend *, qa_error *);
bool frontend_ui_audio_prepare_sound(qa_frontend *, qa_audio_bank *, const char *, qa_game_family, qa_error *);
bool frontend_ui_audio_prepare_q2_source(qa_frontend *, qa_audio_bank *, qa_error *);
bool frontend_ui_audio_prepare_asset(qa_frontend *, qa_audio_asset *, qa_error *);
void frontend_ui_audio_event(void *, const qa_audio_voice_event *);
void frontend_ui_sound(void *, uint32_t, qa_ui_sound);
bool frontend_ui_features_assets_read(const qa_frontend *, qa_audio_asset ***, size_t *, qa_error *);
bool frontend_ui_features_content_visit(const qa_frontend *, const qa_application_content_visitor *, qa_error *);
bool frontend_ui_features_captions(frontend_seat *, const qa_active_caption **, size_t *, qa_error *);
const char *frontend_ui_localize(void *, const char *);
/* Formatted source events are delivered immediately; output is caller-owned
 * scratch. Compiled catalogs enter the actual feature pool. */
bool frontend_ui_source_message(qa_frontend *, uint32_t, const qa_builtin_event *,
    char output[1024], const char **, qa_error *);
bool frontend_ui_source_prompt_text(qa_frontend *, uint32_t, const qa_builtin_event *,
    qa_string_id field, char output[1024], const char **, qa_error *);
#endif
