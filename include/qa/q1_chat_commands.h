#ifndef QA_Q1_CHAT_COMMANDS_H
#define QA_Q1_CHAT_COMMANDS_H
#include "qa/console.h"

typedef enum qa_q1_chat_mode {
    QA_Q1_CHAT_ALL,
    QA_Q1_CHAT_TEAM,
    QA_Q1_CHAT_TELL,
    QA_Q1_CHAT_UNKNOWN
} qa_q1_chat_mode;

static inline const char *qa_q1_chat_command_name(qa_console_dialect dialect, qa_q1_chat_mode mode)
{
    if (dialect != QA_CONSOLE_Q1 && dialect != QA_CONSOLE_QW) return NULL;
    switch (mode) {
    case QA_Q1_CHAT_ALL: return "say";
    case QA_Q1_CHAT_TEAM: return "say_team";
    case QA_Q1_CHAT_TELL: return dialect == QA_CONSOLE_Q1 ? "tell" : NULL;
    case QA_Q1_CHAT_UNKNOWN: return NULL;
    }
    return NULL;
}

static inline qa_q1_chat_mode qa_q1_chat_command_read(qa_console_dialect dialect,
    const char *text, bool fold_case)
{
    if (!text) return QA_Q1_CHAT_UNKNOWN;
    for (qa_q1_chat_mode mode = QA_Q1_CHAT_ALL; mode < QA_Q1_CHAT_UNKNOWN; ++mode) {
        const char *name = qa_q1_chat_command_name(dialect, mode), *cursor = text;
        if (!name) continue;
        for (;;) {
            unsigned char left = (unsigned char)*cursor++, right = (unsigned char)*name++;
            if (fold_case && left >= 'A' && left <= 'Z') left += (unsigned char)('a' - 'A');
            if (left != right) break;
            if (!left) return mode;
        }
    }
    return QA_Q1_CHAT_UNKNOWN;
}
#endif
