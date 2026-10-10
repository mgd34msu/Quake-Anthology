#include "qa/console_io.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

static bool publish_lines(qa_dedicated_console *console, qa_platform_events *events,
    uint64_t time_ns, bool *deferred, qa_error *error) {
    *deferred = false;
    for (;;) {
        qa_bytes line;
        bool present;
        if (!qa_dedicated_console_line_peek(console, &line, &present, error)) return false;
        if (!present) return true;
        qa_platform_event_result result = qa_platform_events_push(events,
            &(qa_sys_event){.kind = QA_PLATFORM_EVENT_CONSOLE_LINE, .time_ns = time_ns}, line, (qa_bytes){0});
        if (result == QA_PLATFORM_EVENT_FULL) { *deferred = true; return true; }
        qa_dedicated_console_line_commit(console);
    }
}

bool qa_platform_console_pump(qa_dedicated_console *console, qa_platform_events *events,
    int descriptor, size_t budget, uint64_t time_ns, qa_error *error) {
    if (descriptor < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid dedicated input descriptor");
        return false;
    }
    bool deferred;
    if (!publish_lines(console, events, time_ns, &deferred, error)) return false;
    if (deferred) return true;
    if (qa_dedicated_console_ended(console))
        return true;
    if (!budget)
        budget = 65536;
    while (budget) {
        struct pollfd input = {.fd = descriptor, .events = POLLIN};
        int ready = poll(&input, 1, 0);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || input.revents & (POLLERR | POLLNVAL)) {
            qa_error_set(error, QA_ERROR_IO, 0, "polling dedicated input: %s",
                         ready < 0 ? strerror(errno) : "descriptor error");
            return false;
        }
        if (!ready || !(input.revents & (POLLIN | POLLHUP)))
            return true;
        uint8_t bytes[4096];
        size_t size = budget < sizeof(bytes) ? budget : sizeof(bytes);
        size_t room = qa_dedicated_console_read_room(console);
        if (size > room) size = room;
        if (qa_platform_events_admit(events, QA_PLATFORM_EVENT_CONSOLE_LINE,
            qa_dedicated_console_line_bound(console, size)) != QA_PLATFORM_EVENT_ACCEPTED) return true;
        ssize_t read_count = read(descriptor, bytes, size);
        if (read_count < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            qa_error_set(error, QA_ERROR_IO, 0, "reading dedicated input: %s", strerror(errno));
            return false;
        }
        if (!qa_dedicated_console_feed(console, (qa_bytes){bytes, (size_t)read_count}, !read_count,
                                       error) || !publish_lines(console, events, time_ns, &deferred, error))
            return false;
        if (!read_count || deferred)
            return true;
        budget -= (size_t)read_count;
    }
    return true;
}
