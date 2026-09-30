#ifndef QA_TOOLS_INTERNAL_H
#define QA_TOOLS_INTERNAL_H
#include "qa/tools.h"
typedef struct tools_console { struct tools_console *next; qa_console *console; } tools_console;
typedef struct tools_capture {
    struct tools_capture *next;
    qa_command_context source;
    char *script, *name;
    qa_capture_format format;
    bool levelshot, silent;
} tools_capture;
struct qa_tools {
    qa_tools_options options;
    qa_console *output_console; /* Borrowed only during a synchronous tool handler. */
    tools_console *consoles;
    tools_capture *captures, **capture_tail;
    qa_camera_document *camera;
    qa_camera_playback *playback;
    qa_profiler *profiler;
    qa_debug_store *debug;
    unsigned busy;
};
bool tools_fail(qa_error *, const char *);
char *tools_copy(const char *, qa_error *);
void tools_print(qa_tools *, const qa_command_context *, const char *);
bool tools_timer_command(qa_tools *, const qa_command_invocation *, qa_error *);
bool tools_diagnostic_command(qa_tools *, const qa_command_invocation *, qa_error *);
bool tools_camera_author(qa_tools *, const qa_command_invocation *, qa_error *);
bool tools_files(qa_tools *, const qa_command_context *, qa_vfs **, qa_mount_id *, qa_error *);
#endif
