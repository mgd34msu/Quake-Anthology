#include "internal.h"
#include "qa/demo.h"

#define DEMO_HEADER_BYTES 32u
#define DEMO_RECORD_HEADER 52u

struct qa_demo_recorder {
    qa_fs_stream *stream;
    uint64_t time_ns, sequence, position;
    bool faulted, ended;
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
    size_t written = 0;
    uint64_t resulting_size = 0;
    bool ok = qa_fs_stream_write(recorder->stream, bytes, recorder->position, &written, &resulting_size, error);
    if (!ok || written != bytes.size || recorder->position > UINT64_MAX - written) {
        recorder->faulted = true;
        if (ok) persistence_fail(error, QA_ERROR_IO, "Demo append made incomplete progress");
        return false;
    }
    recorder->position += written;
    return true;
}

bool qa_demo_record_append(qa_demo_recorder *recorder, qa_demo_record_kind kind, uint64_t elapsed_ns,
                            qa_net_protocol_id protocol, qa_bytes payload, qa_error *error)
{
    if (!recorder || recorder->faulted || recorder->ended || kind < QA_DEMO_KEYFRAME || kind > QA_DEMO_END ||
        (!payload.data && payload.size) || recorder->sequence == UINT64_MAX ||
        payload.size > UINT64_MAX - DEMO_RECORD_HEADER || (kind != QA_DEMO_ADVANCE && elapsed_ns) ||
        recorder->time_ns > UINT64_MAX - elapsed_ns || (kind == QA_DEMO_ADVANCE && payload.size) ||
        (kind == QA_DEMO_END && payload.size) || (!recorder->sequence && kind != QA_DEMO_KEYFRAME))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid or faulted demo append");
    if (!qa_net_protocol_valid(protocol, error)) return false;
    if (kind == QA_DEMO_KEYFRAME) {
        qa_save_image *image = NULL;
        if (!qa_save_image_decode(payload, &image, error)) return false;
        const qa_save_metadata *metadata = qa_save_image_metadata(image);
        bool matches = metadata->elapsed_ns == recorder->time_ns;
        if (!qa_save_image_destroy_checked(&image, error)) return false;
        if (!matches) return persistence_fail(error, QA_ERROR_FORMAT, "Demo keyframe time differs from recording");
    }
    uint8_t header[DEMO_RECORD_HEADER];
    qa_net_writer w;
    qa_net_writer_init(&w, header, sizeof(header), error);
    qa_net_write_u64(&w, DEMO_RECORD_HEADER + payload.size); qa_net_write_u32(&w, kind); qa_net_write_u32(&w, 0);
    qa_net_write_u64(&w, recorder->sequence + 1); qa_net_write_u64(&w, recorder->time_ns + elapsed_ns);
    qa_net_write_u64(&w, elapsed_ns); qa_net_write_u32(&w, protocol.kind);
    qa_net_write_u32(&w, protocol.revision); qa_net_write_u32(&w, protocol.flags);
    if (w.failed || !write_part(recorder, (qa_bytes){header, sizeof(header)}, error) ||
        !write_part(recorder, payload, error)) return false;
    if (!qa_fs_stream_sync(recorder->stream, error)) { recorder->faulted = true; return false; }
    ++recorder->sequence;
    recorder->time_ns += elapsed_ns;
    recorder->ended = kind == QA_DEMO_END;
    return true;
}

bool qa_demo_record_keyframe(qa_demo_recorder *recorder, const qa_save_image *image, qa_error *error)
{
    qa_buffer bytes = {0};
    if (!qa_save_image_encode(image, &bytes, error)) return false;
    bool ok = qa_demo_record_append(recorder, QA_DEMO_KEYFRAME, 0,
        (qa_net_protocol_id){QA_NET_UNIFIED_1, 0, 0}, (qa_bytes){bytes.data, bytes.size}, error);
    qa_buffer_free(&bytes);
    return ok;
}

bool qa_demo_record_begin(qa_fs_root *root, const char *name, const qa_save_image *initial,
                           qa_demo_recorder **out, qa_error *error)
{
    if (!root || !initial || !out || !demo_name(name, error)) return false;
    const qa_save_metadata *metadata = qa_save_image_metadata(initial);
    qa_demo_recorder *recorder = calloc(1, sizeof(*recorder));
    if (!recorder) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating demo recorder");
    uint64_t previous_size;
    if (!qa_fs_root_stream_open(root, name, QA_FS_STREAM_WRITE, false, &recorder->stream, &previous_size, error)) {
        free(recorder); return false;
    }
    recorder->time_ns = metadata->elapsed_ns;
    uint8_t header[DEMO_HEADER_BYTES];
    qa_net_writer w;
    qa_net_writer_init(&w, header, sizeof(header), error);
    qa_net_write_data(&w, "QADM\r\n\032\n", 8); qa_net_write_u32(&w, 0); qa_net_write_u32(&w, DEMO_HEADER_BYTES);
    qa_net_write_u64(&w, metadata->elapsed_ns);
    qa_net_write_u32(&w, 0); qa_net_write_u32(&w, 0);
    if (w.failed || !write_part(recorder, (qa_bytes){header, sizeof(header)}, error) ||
        !qa_demo_record_keyframe(recorder, initial, error)) {
        qa_demo_recorder_destroy(recorder); return false;
    }
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
{ if (recorder) { qa_fs_stream_close(recorder->stream); free(recorder); } }

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
        if (remaining < DEMO_RECORD_HEADER) {
            if (!recover_tail) ok = persistence_fail(error, QA_ERROR_FORMAT, "Truncated shared demo tail");
            break;
        }
        const uint8_t *header = buffer->data + position;
        uint64_t length = qa_load_u64le(header);
        if (length < DEMO_RECORD_HEADER) {
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
            .payload = {header + DEMO_RECORD_HEADER, (size_t)length - DEMO_RECORD_HEADER}};
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
