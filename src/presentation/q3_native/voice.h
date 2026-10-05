#ifndef QA_Q3_NATIVE_VOICE_H
#define QA_Q3_NATIVE_VOICE_H
#include "server_commands.h"
#include "qa/common_parse.h"
#include "qa/source_save.h"

typedef struct q3n_voice_chat {
    char id[64];
    int32_t count, sounds[64];
    char text[64][64];
} q3n_voice_chat;
typedef struct q3n_voice_list {
    char name[64];
    int32_t gender, count;
    q3n_voice_chat chats[64];
} q3n_voice_list;
typedef struct q3n_head_voice { char head[64]; int32_t list; } q3n_head_voice;
typedef struct q3n_buffered_voice {
    int32_t client, sound;
    bool voice_only;
    char command[150], message[150];
} q3n_buffered_voice;
typedef struct q3n_voice {
    q3n_voice_list lists[8];
    q3n_head_voice heads[64];
    q3n_buffered_voice buffer[32];
    int32_t in, out, time;
    bool loaded;
    qa_common_parser parser;
} q3n_voice;

bool q3n_voice_load(q3n_server_commands *, const q3n_frame *, qa_error *);
bool q3n_voice_local(q3n_server_commands *, const q3n_frame *, int32_t,
    bool, int32_t, int32_t, const char *, qa_error *);
bool q3n_voice_finish(q3n_server_commands *, const q3n_frame *, qa_error *);
#endif
