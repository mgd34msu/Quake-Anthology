#ifndef QA_FRONTEND_UI_FEATURES_H
#define QA_FRONTEND_UI_FEATURES_H
#include "internal.h"
#include "qa/media_captions.h"
#include "qa/persistence_content.h"
#include "qa/hud_controls.h"
bool frontend_ui_features_prepare(qa_frontend *, qa_error *);
bool frontend_ui_features_destroy(qa_frontend *, qa_error *);
/* Strict retained-child admission, distinct from returned draw callbacks. */
bool frontend_ui_features_idle(const qa_frontend *);
bool frontend_ui_features_sync(qa_frontend *, qa_error *);
bool frontend_ui_audio_prepare_sound(qa_frontend *, qa_audio_bank *, const char *, qa_game_family, qa_error *);
bool frontend_ui_audio_prepare_q2_source(qa_frontend *, qa_audio_bank *, qa_actor_owner, qa_error *);
bool frontend_ui_audio_prepare_asset(qa_frontend *, qa_audio_asset *, qa_error *);
void frontend_ui_audio_event(void *, const qa_audio_voice_event *);
bool frontend_ui_local_sound(frontend_seat *, qa_audio_bank *, qa_game_family,
    const char *, int32_t channel, bool *found, qa_error *);
void frontend_ui_sound(void *, uint32_t, qa_ui_sound);
bool frontend_sound_command(qa_frontend *, const qa_command_invocation *, bool *, qa_error *);
void frontend_ui_graph_sample(qa_frontend *, float, int32_t);
void frontend_ui_graph_frame(qa_frontend *, uint64_t elapsed_ns);
void frontend_ui_graph_network(qa_frontend *, uint32_t dropped, uint32_t suppressed,
    bool has_ping, double ping_ms);
bool frontend_ui_graph_draw(qa_frontend *, uint32_t physical_seat, qa_scene_frame *, qa_scene_rect, qa_bytes palette, qa_error *);
bool frontend_ui_features_assets_read(const qa_frontend *, qa_audio_asset ***, size_t *, qa_error *);
bool frontend_ui_features_content_visit(const qa_frontend *, const qa_application_content_visitor *, qa_error *);
bool frontend_ui_features_captions(frontend_seat *, const qa_active_caption **, size_t *, qa_error *);
const char *frontend_ui_localize(void *, const char *);
/* Formatted source events are delivered immediately; text is borrowed from
 * frame storage. Compiled catalogs enter the actual feature pool. */
bool frontend_ui_source_message(qa_frontend *, uint32_t, const qa_builtin_event *,
    const char **, qa_error *);
bool frontend_ui_source_text(qa_frontend *, uint32_t, qa_game_family, qa_vfs *, qa_product_edition,
    qa_strings *, const char *, const qa_builtin_message_arg *, size_t,
    qa_arena *, qa_buffer *, qa_error *);
typedef struct frontend_q2_print_policy {
    bool center, chat, notify;
    uint64_t duration_ns;
    qa_hud_center_policy center_policy;
} frontend_q2_print_policy;
frontend_q2_print_policy frontend_ui_q2_print_policy(bool, uint32_t, bool,
    const qa_cvars *, const qa_hud_cvar_handles *);
bool frontend_ui_console_text(qa_arena *, const char *, bool, const char **, qa_error *);
bool frontend_ui_source_prompt_text(qa_frontend *, uint32_t, const qa_builtin_event *,
    qa_string_id field, char output[1024], const char **, qa_error *);
#endif
