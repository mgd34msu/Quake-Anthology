#include "qa/console_io.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

bool qa_dedicated_console_poll(qa_dedicated_console *console, int descriptor, size_t budget,
                               qa_error *error) {
    if (descriptor < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid dedicated input descriptor");
        return false;
    }
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
                                       error))
            return false;
        if (!read_count)
            return true;
        budget -= (size_t)read_count;
    }
    return true;
}
