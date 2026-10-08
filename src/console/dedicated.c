#include "internal.h"
#include "qa/console_io.h"
#include "qa/text.h"
#include "qa/console_dedicated_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
struct qa_dedicated_console {
    qac_text pending, line;
    size_t consumed;
    bool ended;
};
qa_dedicated_console *qa_dedicated_console_create(qa_error *error) {
    qa_dedicated_console *console = calloc(1, sizeof(*console));
    if (!console) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating dedicated console");
        return NULL;
    }
    console->pending.capacity = console->line.capacity = QA_PLATFORM_EVENT_BYTE_CAPACITY + 1u;
    console->pending.data = malloc(console->pending.capacity);
    console->line.data = malloc(console->line.capacity);
    if (!console->pending.data || !console->line.data) {
        qa_dedicated_console_destroy(console);
        qac_fail(error, QA_ERROR_MEMORY, "allocating dedicated console text reservations");
        return NULL;
    }
    console->pending.data[0] = console->line.data[0] = 0;
    return console;
}
void qa_dedicated_console_destroy(qa_dedicated_console *console) {
    if (!console)
        return;
    free(console->pending.data);
    free(console->line.data);
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
bool qa_dedicated_console_line_next(qa_dedicated_console *console, qa_bytes *line,
    bool *present, qa_error *error) {
    *line = (qa_bytes){0};
    *present = false;
    if (console->consumed == console->pending.size) return true;
    const char *start = console->pending.data + console->consumed;
    size_t size = console->pending.size - console->consumed;
    const char *newline = memchr(start, '\n', size);
    if (!newline && !console->ended) return true;
    size_t length = newline ? (size_t)(newline - start) : size;
    size_t consumed = length + (newline ? 1u : 0u);
    if (newline && length && start[length - 1] == '\r') --length;
    console->line.size = 0;
    size_t cursor = 0;
    uint32_t scalar;
    while (qa_utf8_next((qa_bytes){(const uint8_t *)start, length}, &cursor, &scalar)) {
        char encoded[4];
        size_t count = qa_utf8_encode(scalar, encoded);
        if (!qac_text_add(&console->line, encoded, count, error)) return false;
    }
    if (!qac_text_string(&console->line, "\n", error)) return false;
    console->consumed += consumed;
    if (console->consumed == console->pending.size) {
        console->consumed = console->pending.size = 0;
        if (console->pending.data)
            console->pending.data[0] = 0;
    }
    *line = (qa_bytes){(const uint8_t *)console->line.data, console->line.size + 1};
    *present = true;
    return true;
}
static bool dedicated_fields(qa_source_save_io *io, qa_dedicated_console *console)
{
    uint8_t magic[4]={'Q','D','C','N'};
    size_t size=console->pending.size, capacity=console->pending.capacity, consumed=console->consumed;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QDCN",4) ||
        !qa_source_save_count(io,&size,reading?io->input.size-io->offset:SIZE_MAX-1) ||
        !qa_source_save_count(io,&capacity,SIZE_MAX) || !qa_source_save_count(io,&consumed,size) || consumed>size ||
        (capacity?size>=capacity:size!=0) || !qa_source_save_bool(io,&console->ended)) return false;
    if (reading) {
        if (size>io->input.size-io->offset) return false;
        if (capacity) {
            console->pending.data=malloc(capacity);
            if (!console->pending.data) return qac_fail(io->error,QA_ERROR_MEMORY,"Restoring dedicated console input reservation");
        }
        console->pending.size=size; console->pending.capacity=capacity; console->consumed=consumed;
    }
    if ((size && (!console->pending.data || (!reading && memchr(console->pending.data,0,size)))) ||
        !qa_source_save_bytes(io,(uint8_t *)console->pending.data,size)) return false;
    if (size && memchr(console->pending.data,0,size)) return false;
    if (reading && capacity) console->pending.data[size]=0;
    return !consumed || (consumed<size && console->pending.data[consumed-1]=='\n');
}
bool qa_dedicated_console_checkpoint(const qa_dedicated_console *console, qa_buffer *out, qa_error *error)
{
    if (!console || !out || out->data || out->size || console->consumed>console->pending.size ||
        (console->pending.capacity?console->pending.size>=console->pending.capacity:console->pending.size!=0) ||
        (console->pending.capacity!=0)!=(console->pending.data!=NULL) ||
        (console->pending.data && console->pending.data[console->pending.size]))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Dedicated capture requires its actual input owner and empty output");
    qa_source_save_io io={0}; qa_dedicated_console state=*console;
    bool ok=qa_source_save_writer(&io,NULL,error) && dedicated_fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    return ok || qac_fail(error,QA_ERROR_FORMAT,"Dedicated console continuation is inconsistent");
}
bool qa_dedicated_console_restore(qa_dedicated_console *console, qa_bytes bytes, qa_error *error)
{
    if (!console || console->pending.size || console->consumed || console->ended)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Dedicated import requires an empty isolated input owner");
    qa_source_save_io io={0}; qa_dedicated_console state={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && dedicated_fields(&io,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        free(console->pending.data);
        state.line=console->line;
        *console=state;
    }
    else free(state.pending.data);
    return ok || qac_fail(error,QA_ERROR_FORMAT,"Saved dedicated console input is invalid");
}
