/* Port of src/content/q3/base/game/save-callbacks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_CALLBACKS_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_CALLBACKS_H
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
/* TS Q3CallbackFamily<F>: generic becomes qa_q3_entity_callback (fn+ctx) identity by fn+ctx pair. */
typedef struct qa_q3_callback_family {
  struct { char *id; qa_q3_entity_callback callback; } *entries; size_t count, cap;
} qa_q3_callback_family;
void qa_q3_callback_family_init(qa_q3_callback_family *family);
void qa_q3_callback_family_destroy(qa_q3_callback_family *family);
qa_q3_entity_callback qa_q3_callback_family_register(qa_q3_callback_family *family, const char *id, qa_q3_entity_callback callback);
qa_q3_entity_callback qa_q3_callback_family_intern(qa_q3_callback_family *family, const char *id, qa_q3_entity_callback callback);
/* NULL callback.fn = TS null; capture returns borrowed id or NULL. Errors abort. */
const char *qa_q3_callback_family_capture(qa_q3_callback_family *family, qa_q3_entity_callback callback);
qa_q3_entity_callback qa_q3_callback_family_resolve(qa_q3_callback_family *family, const char *id);
typedef struct qa_q3_callback_catalog {
  qa_q3_callback_family think, reached, blocked, touch, use, pain, die;
} qa_q3_callback_catalog;
void qa_q3_callback_catalog_init(qa_q3_callback_catalog *catalog);
void qa_q3_callback_catalog_destroy(qa_q3_callback_catalog *catalog);
#endif
