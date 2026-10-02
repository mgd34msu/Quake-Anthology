#ifndef QA_BOT_CHARACTER_READER_H
#define QA_BOT_CHARACTER_READER_H
#include "internal.h"
#include "qa/source_save.h"
typedef struct bot_character_reader {
    qa_script_services services;
    bool callback_failed, error_reported;
} bot_character_reader;
bool bot_character_reader_create(const qa_script_services *, bot_character_reader **, qa_error *);
qa_script_services bot_character_reader_services(bot_character_reader *);
void bot_character_reader_report(bot_character_reader *, qa_script *, const char *, const qa_error *);
bool bot_character_reader_fields(qa_source_save_io *, qa_script **, bot_character_reader **,
                                 const qa_script_services *);
bool bot_character_reader_copy(qa_script *, const bot_character_reader *, qa_script **,
                               bot_character_reader **, qa_error *);
#endif
