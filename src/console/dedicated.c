#include "internal.h"
#include "qa/console_io.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>
struct qa_dedicated_console {
    qac_text pending;
    size_t consumed;
    bool ended;
};
qa_dedicated_console *qa_dedicated_console_create(qa_error *error) {
    qa_dedicated_console *console = calloc(1, sizeof(*console));
    if (!console)
        qac_fail(error, QA_ERROR_MEMORY, "allocating dedicated console");
    return console;
}
void qa_dedicated_console_destroy(qa_dedicated_console *console) {
    if (!console)
        return;
    free(console->pending.data);
    free(console);
}
bool qa_dedicated_console_feed(qa_dedicated_console *console, qa_bytes bytes, bool ended,
                               qa_error *error) {
    if (console->ended || (bytes.size && (!bytes.data || memchr(bytes.data, 0, bytes.size))))
        return qac_fail(error, QA_ERROR_ARGUMENT, "dedicated console input ended or contains NUL");
    if (console->consumed) {
        console->pending.size -= console->consumed;
        memmove(console->pending.data, console->pending.data + console->consumed,
                console->pending.size + 1);
        console->consumed = 0;
    }
    if (!qac_text_add(&console->pending, (const char *)bytes.data, bytes.size, error))
        return false;
    console->ended = ended;
    return true;
}
bool qa_dedicated_console_eof(const qa_dedicated_console *console) {
    return console->ended && console->consumed == console->pending.size;
}
bool qa_dedicated_console_ended(const qa_dedicated_console *console) { return console->ended; }
bool qa_dedicated_console_drain(qa_dedicated_console *console, qa_console *commands,
                                const qa_command_context *context, size_t *lines, qa_error *error) {
    size_t count = 0;
    if (lines)
        *lines = 0;
    while (console->consumed < console->pending.size) {
        const char *start = console->pending.data + console->consumed;
        size_t size = console->pending.size - console->consumed;
        const char *newline = memchr(start, '\n', size);
        if (!newline && !console->ended)
            break;
        size_t length = newline ? (size_t)(newline - start) : size;
        size_t consumed = length + (newline ? 1u : 0u);
        if (newline && length && start[length - 1] == '\r')
            --length;
        qa_buffer decoded = {0};
        qac_text line = {0};
        bool ok = qa_utf8_repair((qa_bytes){(const uint8_t *)start, length}, &decoded, error) &&
                  qac_text_add(&line, (const char *)decoded.data, decoded.size, error) &&
                  qac_text_string(&line, "\n", error) &&
                  qa_console_append(commands, context, line.data, error);
        qa_buffer_free(&decoded);
        free(line.data);
        if (!ok)
            return false;
        console->consumed += consumed;
        ++count;
        if (lines)
            *lines = count;
    }
    if (console->consumed == console->pending.size) {
        console->consumed = console->pending.size = 0;
        if (console->pending.data)
            console->pending.data[0] = 0;
    }
    return true;
}
