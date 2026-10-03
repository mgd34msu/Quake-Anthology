#include "internal.h"
#include "qa/console_io.h"
#include "qa/text.h"
#include "qa/console_dedicated_save.h"
#include "qa/source_save.h"
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
    if (!console || console->pending.data || console->pending.size || console->pending.capacity || console->consumed || console->ended)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Dedicated import requires an empty isolated input owner");
    qa_source_save_io io={0}; qa_dedicated_console state={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && dedicated_fields(&io,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) *console=state;
    else free(state.pending.data);
    return ok || qac_fail(error,QA_ERROR_FORMAT,"Saved dedicated console input is invalid");
}
