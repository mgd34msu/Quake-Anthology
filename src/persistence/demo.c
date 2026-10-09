#include "internal.h"
#include "qa/demo.h"
#include <SDL_thread.h>
#include <SDL_mutex.h>

#define DEMO_HEADER_BYTES 32u

enum { DEMO_QUEUE_BYTES = 256 * 1024, DEMO_BATCH_BYTES = 64 * 1024 };
struct qa_demo_recorder {
    qa_fs_stream *stream;
    uint64_t time_ns, sequence, position;
    bool faulted, ended, buffered, dirty;
    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_cond *ready, *drained;
    uint8_t *queue;
    size_t first, queued;
    uint64_t written_position, durable_position;
    bool flushing, stopping;
    qa_error worker_error;
};
struct qa_demo {
    qa_buffer storage;
    qa_demo_record *records;
    size_t count, capacity;
    uint64_t start_ns, end_ns;
    bool complete;
};

static bool demo_name(const char *name, qa_error *error)
{
    if (!name || !*name || name[0] == '/')
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Demo requires a contained relative filename");
    const char *component = name;
    for (const char *p = name;; ++p) {
        if (*p == '\\' || *p == ':' || ((unsigned char)*p < 32 && *p))
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid demo filename");
        if (*p != '/' && *p) continue;
        size_t size = (size_t)(p - component);
        if (!size || (size == 1 && component[0] == '.') ||
            (size == 2 && component[0] == '.' && component[1] == '.'))
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid demo path component");
        if (!*p) return true;
        component = p + 1;
    }
}

static bool write_part(qa_demo_recorder *recorder, qa_bytes bytes, qa_error *error)
{
    while (bytes.size) {
        size_t written = 0;
        if (!qa_fs_stream_write_some(recorder->stream, bytes, recorder->written_position,
            &written, error) || !written) {
            if (error && error->code == QA_OK)
                persistence_fail(error, QA_ERROR_IO, "Demo append made incomplete progress");
            return false;
        }
        recorder->written_position += written;
        bytes.data += written; bytes.size -= written;
    }
    return true;
}

static int SDLCALL record_worker(void *context)
{
    qa_demo_recorder *recorder = context;
    SDL_LockMutex(recorder->mutex);
    for (;;) {
        while (recorder->queued < DEMO_BATCH_BYTES && !recorder->flushing && !recorder->stopping)
            SDL_CondWait(recorder->ready, recorder->mutex);
        if (!recorder->queued) {
            if (recorder->written_position != recorder->durable_position) {
                SDL_UnlockMutex(recorder->mutex);
                qa_error error = {0};
                bool ok = qa_fs_stream_sync(recorder->stream, &error);
                SDL_LockMutex(recorder->mutex);
                if (!ok) {
                    recorder->worker_error = error;
                    SDL_CondBroadcast(recorder->drained);
                    break;
                }
                recorder->durable_position = recorder->written_position;
            }
            recorder->flushing = false;
            SDL_CondBroadcast(recorder->drained);
            if (recorder->stopping) break;
            continue;
        }
        size_t amount = recorder->queued;
        size_t first = recorder->first;
        size_t part = amount < DEMO_QUEUE_BYTES - first ? amount : DEMO_QUEUE_BYTES - first;
        SDL_UnlockMutex(recorder->mutex);
        qa_error error = {0};
        bool ok = write_part(recorder, (qa_bytes){recorder->queue + first, part}, &error) &&
            write_part(recorder, (qa_bytes){recorder->queue, amount - part}, &error) &&
            qa_fs_stream_sync(recorder->stream, &error);
        SDL_LockMutex(recorder->mutex);
        if (!ok) {
            recorder->worker_error = error;
            SDL_CondBroadcast(recorder->drained);
            break;
        }
        recorder->first = (first + amount) % DEMO_QUEUE_BYTES;
        recorder->queued -= amount;
        recorder->durable_position = recorder->written_position;
    }
    SDL_UnlockMutex(recorder->mutex);
    return 0;
}

static bool record_worker_start(qa_demo_recorder *recorder, qa_error *error)
{
    if (recorder->thread) return true;
    recorder->queue = malloc(DEMO_QUEUE_BYTES);
    recorder->mutex = SDL_CreateMutex();
    recorder->ready = SDL_CreateCond();
    recorder->drained = SDL_CreateCond();
    if (recorder->queue && recorder->mutex && recorder->ready && recorder->drained)
        recorder->thread = SDL_CreateThread(record_worker, "Demo writer", recorder);
    if (recorder->thread) return true;
    recorder->faulted = true;
    return persistence_fail(error, QA_ERROR_MEMORY, "Creating buffered demo writer");
}

static void queue_part(qa_demo_recorder *recorder, qa_bytes bytes, size_t at)
{
    size_t first = bytes.size < DEMO_QUEUE_BYTES - at ? bytes.size : DEMO_QUEUE_BYTES - at;
    if (first) memcpy(recorder->queue + at, bytes.data, first);
    if (bytes.size != first) memcpy(recorder->queue, bytes.data + first, bytes.size - first);
}

static bool queue_record(qa_demo_recorder *recorder, qa_bytes header, qa_bytes payload, qa_error *error)
{
    SDL_LockMutex(recorder->mutex);
    bool ok = recorder->worker_error.code == QA_OK;
    if (!ok && error) *error = recorder->worker_error;
    size_t size = header.size + payload.size;
    if (ok && size > DEMO_QUEUE_BYTES - recorder->queued)
        ok = persistence_fail(error, QA_ERROR_IO, "Demo writer queue is full; completed file prefix remains recoverable");
    if (ok) {
        size_t at = (recorder->first + recorder->queued) % DEMO_QUEUE_BYTES;
        queue_part(recorder, header, at);
        queue_part(recorder, payload, (at + header.size) % DEMO_QUEUE_BYTES);
        recorder->queued += size;
        if (recorder->queued >= DEMO_BATCH_BYTES) SDL_CondSignal(recorder->ready);
    }
    SDL_UnlockMutex(recorder->mutex);
    return ok;
}

static bool record_header(qa_net_writer *writer, qa_demo_record_kind kind,
    uint64_t sequence, uint64_t time_ns, uint64_t elapsed_ns,
    qa_net_protocol_id protocol, size_t payload_size)
{
    qa_net_write_u64(writer, PERSISTENCE_DEMO_RECORD_HEADER + payload_size); qa_net_write_u32(writer, kind); qa_net_write_u32(writer, 0);
    qa_net_write_u64(writer, sequence); qa_net_write_u64(writer, time_ns);
    qa_net_write_u64(writer, elapsed_ns); qa_net_write_u32(writer, protocol.kind);
    qa_net_write_u32(writer, protocol.revision); qa_net_write_u32(writer, protocol.flags);
    return !writer->failed;
}

static bool record_append(qa_demo_recorder *recorder, qa_demo_record_kind kind, uint64_t elapsed_ns,
                            qa_net_protocol_id protocol, qa_bytes payload, qa_error *error)
{
    if (!recorder || recorder->faulted || recorder->ended || kind < QA_DEMO_KEYFRAME || kind > QA_DEMO_END ||
        (!payload.data && payload.size) || recorder->sequence == UINT64_MAX ||
        payload.size > UINT64_MAX - PERSISTENCE_DEMO_RECORD_HEADER || (kind != QA_DEMO_ADVANCE && elapsed_ns) ||
        recorder->time_ns > UINT64_MAX - elapsed_ns || (kind == QA_DEMO_ADVANCE && payload.size) ||
        (kind == QA_DEMO_END && payload.size) || (!recorder->sequence && kind != QA_DEMO_KEYFRAME))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid or faulted demo append");
    if (!qa_net_protocol_valid(protocol, error)) return false;
    uint8_t header[PERSISTENCE_DEMO_RECORD_HEADER];
    qa_net_writer w;
    qa_net_writer_init(&w, header, sizeof(header), error);
    if (!record_header(&w, kind, recorder->sequence + 1,
        recorder->time_ns + elapsed_ns, elapsed_ns, protocol, payload.size)) return false;
    if (recorder->position > UINT64_MAX - sizeof(header) - payload.size)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Demo position overflows");
    bool queued = recorder->buffered && kind != QA_DEMO_KEYFRAME;
    bool ok = queued ? queue_record(recorder, (qa_bytes){header, sizeof(header)}, payload, error) :
        qa_demo_record_flush(recorder, error) && write_part(recorder, (qa_bytes){header, sizeof(header)}, error) &&
        write_part(recorder, payload, error);
    if (!ok) { recorder->faulted = true; return false; }
    recorder->position += sizeof(header) + payload.size;
    recorder->dirty = true;
    if ((!recorder->buffered || kind == QA_DEMO_KEYFRAME || kind == QA_DEMO_END) &&
        !qa_demo_record_flush(recorder, error)) return false;
    ++recorder->sequence;
    recorder->time_ns += elapsed_ns;
    recorder->ended = kind == QA_DEMO_END;
    return true;
}

bool qa_demo_record_append(qa_demo_recorder *recorder, qa_demo_record_kind kind, uint64_t elapsed_ns,
    qa_net_protocol_id protocol, qa_bytes payload, qa_error *error)
{
    if (!recorder) return persistence_fail(error, QA_ERROR_ARGUMENT, "Absent demo recorder");
    if (kind == QA_DEMO_KEYFRAME) {
        qa_save_image *image = NULL;
        if (!qa_save_image_decode(payload, &image, error)) return false;
        const qa_save_metadata *metadata = qa_save_image_metadata(image);
        bool matches = metadata->elapsed_ns == recorder->time_ns;
        if (!qa_save_image_destroy_checked(&image, error)) return false;
        if (!matches) return persistence_fail(error, QA_ERROR_FORMAT, "Demo keyframe time differs from recording");
    }
    return record_append(recorder, kind, elapsed_ns, protocol, payload, error);
}

bool qa_demo_record_flush(qa_demo_recorder *recorder, qa_error *error)
{
    if (!recorder || recorder->faulted)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Absent or faulted demo recorder");
    if (recorder->thread) {
        SDL_LockMutex(recorder->mutex);
        recorder->flushing = true;
        SDL_CondSignal(recorder->ready);
        while (recorder->flushing && recorder->worker_error.code == QA_OK)
            SDL_CondWait(recorder->drained, recorder->mutex);
        bool ok = recorder->worker_error.code == QA_OK;
        if (!ok && error) *error = recorder->worker_error;
        SDL_UnlockMutex(recorder->mutex);
        if (!ok) { recorder->faulted = true; return false; }
        recorder->dirty = false;
        return true;
    }
    if (!recorder->dirty) return true;
    if (!qa_fs_stream_sync(recorder->stream, error)) {
        recorder->faulted = true;
        return false;
    }
    recorder->dirty = false;
    return true;
}

bool qa_demo_record_buffered(qa_demo_recorder *recorder, bool buffered, qa_error *error)
{
    if (!recorder || recorder->faulted || recorder->ended)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Buffering requires an active demo recorder");
    if (buffered && !record_worker_start(recorder, error)) return false;
    if (!buffered && !qa_demo_record_flush(recorder, error)) return false;
    recorder->buffered = buffered;
    return true;
}

bool qa_demo_record_keyframe(qa_demo_recorder *recorder, const qa_save_image *image, qa_error *error)
{
    if (!recorder || !image || qa_save_image_metadata(image)->elapsed_ns != recorder->time_ns)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Demo keyframe time differs from recording");
    qa_bytes bytes = {0};
    return qa_save_image_encode(image, &bytes, error) &&
        record_append(recorder, QA_DEMO_KEYFRAME, 0,
            (qa_net_protocol_id){QA_NET_UNIFIED_1, 0, 0}, bytes, error);
}

bool qa_demo_record_begin(qa_fs_root *root, const char *name, const qa_save_image *initial,
                           qa_demo_recorder **out, qa_error *error)
{
    if (!root || !initial || !out || !demo_name(name, error)) return false;
    qa_bytes bytes = {0};
    if (!qa_save_image_encode(initial, &bytes, error)) return false;
    if (bytes.size > SIZE_MAX - DEMO_HEADER_BYTES - PERSISTENCE_DEMO_RECORD_HEADER)
        return persistence_fail(error, QA_ERROR_MEMORY, "Initial recording size overflows");
    const qa_save_metadata *metadata = qa_save_image_metadata(initial);
    qa_demo_recorder *recorder = calloc(1, sizeof(*recorder));
    if (!recorder) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating demo recorder");
    size_t size = DEMO_HEADER_BYTES + PERSISTENCE_DEMO_RECORD_HEADER + bytes.size;
    uint8_t *data = malloc(size);
    if (!data) { free(recorder); return persistence_fail(error, QA_ERROR_MEMORY, "Allocating initial recording"); }
    qa_net_writer writer;
    qa_net_writer_init(&writer, data, size, error);
    qa_net_write_data(&writer, "QADM\r\n\032\n", 8);
    qa_net_write_u32(&writer, 0); qa_net_write_u32(&writer, DEMO_HEADER_BYTES);
    qa_net_write_u64(&writer, metadata->elapsed_ns);
    qa_net_write_u32(&writer, 0); qa_net_write_u32(&writer, 0);
    bool ok = !writer.failed && record_header(&writer, QA_DEMO_KEYFRAME, 1,
        metadata->elapsed_ns, 0, (qa_net_protocol_id){QA_NET_UNIFIED_1, 0, 0}, bytes.size) &&
        qa_net_write_data(&writer, bytes.data, bytes.size) &&
        qa_fs_root_replace(root, name, (qa_bytes){data, size}, metadata->elapsed_ns, error);
    free(data);
    uint64_t stored_size = 0;
    if (ok) ok = qa_fs_root_stream_open(root, name, QA_FS_STREAM_WRITE, true,
        &recorder->stream, &stored_size, error);
    if (ok && stored_size != size)
        ok = persistence_fail(error, QA_ERROR_IO, "Initial recording changed before append admission");
    if (!ok) { qa_demo_recorder_destroy(recorder); return false; }
    recorder->time_ns = metadata->elapsed_ns;
    recorder->sequence = 1;
    recorder->durable_position = recorder->written_position = recorder->position = size;
    *out = recorder;
    return true;
}

bool qa_demo_record_end(qa_demo_recorder *recorder, qa_error *error)
{
    if (!recorder) return persistence_fail(error, QA_ERROR_ARGUMENT, "Absent demo recorder");
    if (recorder->ended) return true;
    return qa_demo_record_append(recorder, QA_DEMO_END, 0, (qa_net_protocol_id){QA_NET_UNIFIED_1, 0, 0}, (qa_bytes){0}, error);
}
void qa_demo_recorder_destroy(qa_demo_recorder *recorder)
{
    if (!recorder) return;
    if (recorder->thread) {
        SDL_LockMutex(recorder->mutex);
        recorder->stopping = true;
        SDL_CondSignal(recorder->ready);
        SDL_UnlockMutex(recorder->mutex);
        SDL_WaitThread(recorder->thread, NULL);
    }
    if (recorder->drained) SDL_DestroyCond(recorder->drained);
    if (recorder->ready) SDL_DestroyCond(recorder->ready);
    if (recorder->mutex) SDL_DestroyMutex(recorder->mutex);
    free(recorder->queue);
    qa_fs_stream_close(recorder->stream); free(recorder);
}
uint64_t persistence_demo_record_bytes(const qa_demo_recorder *recorder)
{ return recorder ? recorder->position : 0; }

void qa_demo_destroy(qa_demo *demo)
{ if (demo) { qa_buffer_free(&demo->storage); free(demo->records); free(demo); } }
size_t qa_demo_record_count(const qa_demo *demo) { return demo ? demo->count : 0; }
const qa_demo_record *qa_demo_record_at(const qa_demo *demo, size_t index)
{ return demo && index < demo->count ? demo->records + index : NULL; }
bool qa_demo_complete(const qa_demo *demo) { return demo && demo->complete; }
uint64_t qa_demo_start_time(const qa_demo *demo) { return demo ? demo->start_ns : 0; }
uint64_t qa_demo_end_time(const qa_demo *demo) { return demo ? demo->end_ns : 0; }

static bool retain_record(qa_demo *demo, qa_demo_record record, qa_error *error)
{
    if (demo->count == demo->capacity) {
        size_t capacity = demo->capacity ? demo->capacity * 2 : 128;
        if (capacity < demo->capacity || capacity > SIZE_MAX / sizeof(*demo->records))
            return persistence_fail(error, QA_ERROR_MEMORY, "Demo record index size overflow");
        qa_demo_record *records = realloc(demo->records, capacity * sizeof(*records));
        if (!records) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating demo record index");
        demo->records = records; demo->capacity = capacity;
    }
    demo->records[demo->count++] = record;
    demo->end_ns = record.time_ns;
    demo->complete = record.kind == QA_DEMO_END;
    return true;
}

bool qa_demo_take(qa_buffer *buffer, bool recover_tail, qa_demo **out, qa_error *error)
{
    if (!buffer || !out || !buffer->data || buffer->size < DEMO_HEADER_BYTES ||
        memcmp(buffer->data, "QADM\r\n\032\n", 8) ||
        qa_load_u32le(buffer->data + 12) != DEMO_HEADER_BYTES || qa_load_u64le(buffer->data + 24))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid shared demo header");
    qa_demo *demo = calloc(1, sizeof(*demo));
    if (!demo) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating shared demo");
    demo->start_ns = demo->end_ns = qa_load_u64le(buffer->data + 16);
    size_t position = DEMO_HEADER_BYTES;
    bool ok = true;
    while (position < buffer->size) {
        size_t remaining = buffer->size - position;
        if (demo->complete) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Trailing data after shared demo end marker"); break;
        }
        if (remaining < PERSISTENCE_DEMO_RECORD_HEADER) {
            if (!recover_tail) ok = persistence_fail(error, QA_ERROR_FORMAT, "Truncated shared demo tail");
            break;
        }
        const uint8_t *header = buffer->data + position;
        uint64_t length = qa_load_u64le(header);
        if (length < PERSISTENCE_DEMO_RECORD_HEADER) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Invalid shared demo block extent or trailing data"); break;
        }
        if (length > remaining) {
            if (!recover_tail) ok = persistence_fail(error, QA_ERROR_FORMAT, "Truncated shared demo block");
            break;
        }
        qa_demo_record record = {.kind = (qa_demo_record_kind)qa_load_u32le(header + 8),
            .sequence = qa_load_u64le(header + 16), .time_ns = qa_load_u64le(header + 24),
            .elapsed_ns = qa_load_u64le(header + 32),
            .protocol = {(qa_net_protocol)qa_load_u32le(header + 40), qa_load_u32le(header + 44), qa_load_u32le(header + 48)},
            .payload = {header + PERSISTENCE_DEMO_RECORD_HEADER, (size_t)length - PERSISTENCE_DEMO_RECORD_HEADER}};
        if (qa_load_u32le(header + 12) || record.kind < QA_DEMO_KEYFRAME || record.kind > QA_DEMO_END ||
            record.sequence != demo->count + 1 || demo->end_ns > UINT64_MAX - record.elapsed_ns ||
            record.time_ns != demo->end_ns + record.elapsed_ns ||
            (record.kind != QA_DEMO_ADVANCE && record.elapsed_ns) ||
            ((record.kind == QA_DEMO_ADVANCE || record.kind == QA_DEMO_END) && record.payload.size) ||
            (!demo->count && record.kind != QA_DEMO_KEYFRAME) || !qa_net_protocol_valid(record.protocol, error)) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Invalid shared demo record timing/type/protocol"); break;
        }
        if (record.kind == QA_DEMO_KEYFRAME) {
            qa_save_image *image = NULL;
            ok = qa_save_image_decode(record.payload, &image, error);
            if (!ok) break;
            const qa_save_metadata *metadata = qa_save_image_metadata(image);
            bool matches = metadata->elapsed_ns == record.time_ns;
            if (!qa_save_image_destroy_checked(&image, error)) { ok = false; break; }
            if (!matches) { ok = persistence_fail(error, QA_ERROR_FORMAT, "Shared demo keyframe time mismatch"); break; }
        }
        if (!retain_record(demo, record, error)) { ok = false; break; }
        position += (size_t)length;
    }
    if (ok && (!demo->count || (!demo->complete && !recover_tail)))
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Shared demo has no complete initial state or end marker");
    if (!ok) { qa_demo_destroy(demo); return false; }
    demo->storage = *buffer; *buffer = (qa_buffer){0}; *out = demo;
    return true;
}

bool qa_demo_read(qa_fs_root *root, const char *name, bool recover_tail, qa_demo **out, qa_error *error)
{
    if (!root || !out || !demo_name(name, error)) return false;
    qa_fs_file *file = NULL;
    qa_fs_identity identity;
    if (!qa_fs_root_file_open(root, name, &file, &identity, error)) return false;
    qa_buffer buffer = {0};
    bool ok = qa_fs_file_read_snapshot(file, &identity, &buffer, error);
    qa_fs_file_close(file);
    if (ok) ok = qa_demo_take(&buffer, recover_tail, out, error);
    qa_buffer_free(&buffer);
    return ok;
}

bool qa_demo_seek(const qa_demo *demo, uint64_t target_ns, void *context, const qa_demo_seek_ops *ops,
                   uint64_t *reached_ns, qa_error *error)
{
    if (!demo || !ops || !ops->create || !ops->apply || !ops->finish || !ops->publish || !ops->discard ||
        !reached_ns || target_ns < demo->start_ns || target_ns > demo->end_ns)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid shared demo seek target/producer");
    size_t keyframe = 0;
    for (size_t i = 0; i < demo->count; ++i)
        if (demo->records[i].kind == QA_DEMO_KEYFRAME && demo->records[i].time_ns <= target_ns) keyframe = i;
    qa_save_image *image = NULL;
    if (!qa_save_image_decode(demo->records[keyframe].payload, &image, error)) return false;
    void *candidate = NULL;
    bool ok = ops->create(context, image, &candidate, error);
    if (!qa_save_image_destroy_checked(&image, error)) ok = false;
    if (!ok || !candidate) {
        if (candidate) ops->discard(context, candidate);
        if (ok) persistence_fail(error, QA_ERROR_FORMAT, "Demo restore returned no candidate");
        return false;
    }
    uint64_t reached = demo->records[keyframe].time_ns;
    for (size_t i = keyframe + 1; ok && i < demo->count; ++i) {
        const qa_demo_record *record = demo->records + i;
        if (record->time_ns > target_ns) break;
        if (record->kind == QA_DEMO_KEYFRAME || record->kind == QA_DEMO_END) continue;
        ok = ops->apply(context, candidate, record, error);
        if (ok && record->kind == QA_DEMO_ADVANCE) reached = record->time_ns;
    }
    if (ok) ok = ops->finish(context, candidate, error);
    if (ok) ok = ops->publish(context, candidate, error);
    if (!ok) { ops->discard(context, candidate); return false; }
    *reached_ns = reached;
    return true;
}
