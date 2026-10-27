/* Port of src/content/q3/base/game/format.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_FORMAT_H
#define QA_CONTENT_Q3_BASE_GAME_FORMAT_H
#include <stddef.h>
/* TS GameFormatArgument = number | string | null. */
typedef enum qa_q3_game_format_arg_kind { QA_Q3_FMT_NUMBER, QA_Q3_FMT_STRING, QA_Q3_FMT_NULL } qa_q3_game_format_arg_kind;
typedef struct qa_q3_game_format_arg { qa_q3_game_format_arg_kind kind; double number; const char *string; } qa_q3_game_format_arg;
qa_q3_game_format_arg qa_q3_game_format_number(double value);
qa_q3_game_format_arg qa_q3_game_format_string(const char *value);
qa_q3_game_format_arg qa_q3_game_format_null(void);
/* TS gameFormat(format,args,maxBytes=32000). Returns malloc'd string (NULL=OOM); errors abort via qa_range_error. */
char *qa_q3_game_format(const char *format, const qa_q3_game_format_arg *args, size_t arg_count, double max_bytes);
#endif
