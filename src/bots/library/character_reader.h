#ifndef QA_BOT_CHARACTER_READER_H
#define QA_BOT_CHARACTER_READER_H
#include "internal.h"
typedef struct bot_character_reader {
    qa_script_services services;
    bool callback_failed;
} bot_character_reader;
bool bot_character_reader_create(const qa_script_services *, bot_character_reader **, qa_error *);
qa_script_services bot_character_reader_services(bot_character_reader *);
#endif
