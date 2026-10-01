#ifndef QA_BOT_CHARACTER_LOAD_INTERNAL_H
#define QA_BOT_CHARACTER_LOAD_INTERNAL_H
#include "qa/bot_library.h"

bool bot_character_load(qa_bot_library *, const char *, float, qa_bot_character **,
                        bool *interpolated, qa_error *);
#endif
