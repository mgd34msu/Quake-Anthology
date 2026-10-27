/* Port of src/content/q3/base/game/save-reader.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_READER_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_READER_H
#include "qa/content/q3/base/game/save_state.h"
#include "qa/persistence/value.h"
qa_q3_saved_actor_ref qa_q3_read_actor(const qa_save_reader *reader);
qa_q3_graph_state qa_q3_read_graph(const qa_save_value *value);
#endif
