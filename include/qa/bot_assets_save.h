#ifndef QA_BOT_ASSETS_SAVE_H
#define QA_BOT_ASSETS_SAVE_H

#include "qa/bot_library.h"

/* Explicit immutable resource and learned-weight field codecs. Restore runs
 * no script, file, diagnostic or RNG callbacks. Outputs remain unchanged on
 * failure; restored objects use their ordinary reference-counted release. */
bool qa_bot_weights_save_capture(const qa_bot_weights *, qa_buffer *, qa_error *);
bool qa_bot_weights_save_restore(qa_bytes, qa_bot_weights **, qa_error *);
bool qa_bot_character_save_capture(const qa_bot_character *, qa_buffer *, qa_error *);
bool qa_bot_character_save_restore(qa_bytes, qa_bot_character **, qa_error *);
bool qa_bot_weapons_save_capture(const qa_bot_weapons *, qa_buffer *, qa_error *);
bool qa_bot_weapons_save_restore(qa_bytes, qa_bot_weapons **, qa_error *);
bool qa_bot_items_save_capture(const qa_bot_items *, qa_buffer *, qa_error *);
bool qa_bot_items_save_restore(qa_bytes, qa_bot_items **, qa_error *);

#endif
