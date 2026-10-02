#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include "qa/native_process_platform.h"
#include "qa/source_save.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct platform_handle { size_t references; } platform_handle;
typedef struct platform_stream {
    struct qa_native_process_platform *owner;
    uint64_t id;
    int descriptor;
    uint32_t mode;
    uint64_t device, object;
    bool closed, durable_object;
    platform_handle *held;
} platform_stream;
struct qa_native_process_platform {
    uint64_t id;
    size_t references, busy;
    int64_t frequency;
    platform_stream streams[3];
    bool closing, failed, terminal;
};
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool stream_identity(int descriptor, uint64_t *device, uint64_t *object)
{
#if defined(_WIN32)
    intptr_t handle = _get_osfhandle(descriptor);
    BY_HANDLE_FILE_INFORMATION info;
    if (handle == -1) return false;
    if (GetFileType((HANDLE)handle) == FILE_TYPE_DISK) {
        if (!GetFileInformationByHandle((HANDLE)handle, &info)) return false;
        *device = info.dwVolumeSerialNumber;
        *object = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow;
    } else {
        /* This is the actual retained stream handle, never a source HANDLE. */
        *device = GetFileType((HANDLE)handle); *object = (uint64_t)handle;
    }
#else
    struct stat info;
    if (fstat(descriptor, &info)) return false;
    *device = (uint64_t)info.st_dev; *object = (uint64_t)info.st_ino;
#endif
    return true;
}
bool qa_native_process_platform_current(const qa_native_process_platform *owner, qa_error *error)
{
    if (!owner || owner->closing || owner->failed)
        return fail(error, QA_ERROR_ARGUMENT, "Native platform resource owner is retiring");
    for (size_t i = 0; i < 3; ++i) {
        const platform_stream *stream = owner->streams + i;
        if (stream->closed) continue;
        uint64_t device, object;
        if (!stream_identity(stream->descriptor, &device, &object) ||
            device != stream->device || object != stream->object)
            return fail(error, QA_ERROR_IO, "Native standard descriptor lost its retained object");
    }
    return true;
}
bool qa_native_process_platform_retained(const qa_native_process_platform *owner, qa_error *error)
{
    return (owner && !owner->closing && !owner->failed && !owner->busy) ||
        fail(error, QA_ERROR_ARGUMENT, "Native platform retained owner is unavailable");
}
bool qa_native_process_platform_create(const qa_native_process_platform_options *options,
    qa_native_process_platform **out, qa_error *error)
{
    if (!options || !options->id || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Native platform requires its assigned owner identity");
    for (size_t i = 0; i < 3; ++i) {
        if (!options->standard_ids[i]) return fail(error, QA_ERROR_ARGUMENT, "Native standard capability identity is absent");
        for (size_t j = 0; j < i; ++j) if (options->standard_ids[i] == options->standard_ids[j])
            return fail(error, QA_ERROR_ARGUMENT, "Native standard capability identities repeat");
    }
    qa_native_process_platform *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Owning native platform capabilities");
    owner->id = options->id; owner->references = 1; owner->failed = true;
    for (size_t i = 0; i < 3; ++i) owner->streams[i].closed = true;
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        free(owner); return fail(error, QA_ERROR_IO, "Reading actual performance-counter frequency");
    }
    owner->frequency = frequency.QuadPart;
#else
    owner->frequency = INT64_C(1000000000);
#endif
    *out = owner;
    for (size_t i = 0; i < 3; ++i) {
        platform_stream *stream = owner->streams + i;
        stream->owner = owner; stream->id = options->standard_ids[i];
        stream->mode = i ? 2 : 1;
#if defined(_WIN32)
        stream->descriptor = _dup((int)i);
#else
        stream->descriptor = fcntl((int)i, F_DUPFD_CLOEXEC, 3);
#endif
        if (stream->descriptor < 0)
            return fail(error, QA_ERROR_IO, "Duplicating actual native standard descriptor");
        stream->closed = false;
        stream->held = malloc(sizeof(*stream->held));
        if (!stream->held) return fail(error, QA_ERROR_MEMORY, "Owning actual native standard descriptor holds");
        stream->held->references = 1;
#if defined(_WIN32)
        if (_setmode(stream->descriptor, _O_BINARY) == -1)
            return fail(error, QA_ERROR_IO, "Selecting byte I/O on the owned native standard descriptor");
#endif
        if (!stream_identity(stream->descriptor, &stream->device, &stream->object))
            return fail(error, QA_ERROR_IO, "Reading retained native standard object identity");
#if defined(_WIN32)
        stream->durable_object = GetFileType((HANDLE)_get_osfhandle(stream->descriptor)) == FILE_TYPE_DISK;
#else
        stream->durable_object = true;
#endif
    }
#if defined(_WIN32)
    owner->terminal = _isatty(owner->streams[1].descriptor) != 0;
#else
    owner->terminal = isatty(owner->streams[1].descriptor) != 0;
#endif
    owner->failed = false; return true;
}
bool qa_native_process_platform_capture(const qa_native_process_platform *source,
    qa_native_process_platform **out, qa_error *error)
{
    if (!out || *out || !qa_native_process_platform_retained(source, error)) return false;
    qa_native_process_platform *copy = malloc(sizeof(*copy));
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Retaining native platform stream graph");
    *copy = *source; copy->references = 1; copy->busy = 0;
    for (size_t i = 0; i < 3; ++i) {
        copy->streams[i].owner = copy;
        if (!copy->streams[i].closed && copy->streams[i].held) ++copy->streams[i].held->references;
    }
    *out = copy; return true;
}
bool qa_native_process_platform_entropy(void *context, void *out, size_t bytes, qa_error *error)
{
    qa_native_process_platform *owner = context;
    if ((!out && bytes) || !qa_native_process_platform_current(owner, error)) return false;
#if defined(_WIN32)
    size_t offset = 0;
    while (offset < bytes) {
        ULONG count = bytes - offset > ULONG_MAX ? ULONG_MAX : (ULONG)(bytes - offset);
        if (BCryptGenRandom(NULL, (PUCHAR)out + offset, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return fail(error, QA_ERROR_IO, "Reading actual platform entropy");
        offset += count;
    }
#elif defined(__APPLE__)
    if (bytes) arc4random_buf(out, bytes);
#elif defined(__linux__)
    size_t offset = 0;
    while (offset < bytes) {
        size_t count = bytes - offset > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes - offset;
        ssize_t received = getrandom((uint8_t *)out + offset, count, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return fail(error, QA_ERROR_IO, "Reading actual platform entropy");
        offset += (size_t)received;
    }
#else
    return fail(error, QA_ERROR_UNSUPPORTED, "Native platform entropy producer is unavailable on this operating system");
#endif
    return true;
}
bool qa_native_process_platform_milliseconds(void *context, int64_t *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
#if defined(_WIN32)
    FILETIME time; GetSystemTimeAsFileTime(&time);
    uint64_t ticks = (uint64_t)time.dwHighDateTime << 32 | time.dwLowDateTime;
    uint64_t epoch = UINT64_C(116444736000000000);
    uint64_t delta = ticks >= epoch ? ticks - epoch : epoch - ticks;
    if (delta / 10000 > INT64_MAX) return fail(error, QA_ERROR_UNSUPPORTED, "Platform wall clock exceeds signed milliseconds");
    *out = ticks >= epoch ? (int64_t)(delta / 10000) : -(int64_t)((delta + 9999) / 10000);
#else
    struct timespec time;
    if (clock_gettime(CLOCK_REALTIME, &time)) return fail(error, QA_ERROR_IO, "Reading actual platform wall clock");
    if (time.tv_sec > INT64_MAX / 1000 || time.tv_sec < INT64_MIN / 1000)
        return fail(error, QA_ERROR_UNSUPPORTED, "Platform wall clock exceeds signed milliseconds");
    int64_t base = (int64_t)time.tv_sec * 1000, fraction = time.tv_nsec / 1000000;
    if (base > INT64_MAX - fraction) return fail(error, QA_ERROR_UNSUPPORTED, "Platform wall clock exceeds signed milliseconds");
    *out = base + fraction;
#endif
    return true;
}
bool qa_native_process_platform_seconds(void *context, int64_t *out, qa_error *error)
{
    int64_t milliseconds;
    if (!out || !qa_native_process_platform_milliseconds(context, &milliseconds, error)) return false;
    *out = milliseconds / 1000 - (milliseconds % 1000 < 0); return true;
}
bool qa_native_process_platform_performance(void *context, int64_t *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
#if defined(_WIN32)
    LARGE_INTEGER value;
    if (!QueryPerformanceCounter(&value)) return fail(error, QA_ERROR_IO, "Reading actual performance counter");
    *out = value.QuadPart;
#else
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return fail(error, QA_ERROR_IO, "Reading actual performance counter");
    if (time.tv_sec < 0 || time.tv_sec > INT64_MAX / INT64_C(1000000000) ||
        (int64_t)time.tv_sec * INT64_C(1000000000) > INT64_MAX - time.tv_nsec)
        return fail(error, QA_ERROR_UNSUPPORTED, "Actual performance counter exceeds signed native value");
    *out = (int64_t)time.tv_sec * INT64_C(1000000000) + time.tv_nsec;
#endif
    return true;
}
int64_t qa_native_process_platform_frequency(const qa_native_process_platform *owner)
{ return owner ? owner->frequency : 0; }
/* Date arithmetic is used only to compare the actual libc local/UTC calendar
 * rows; timezone and daylight values are never chosen from a fixed locale. */
static int64_t civil_days(int64_t year, unsigned month, unsigned day)
{
    int64_t y = year; y -= month <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned within = (unsigned)(y - era * 400);
    unsigned shifted = month > 2 ? month - 3 : month + 9;
    unsigned days = (153 * shifted + 2) / 5 + day - 1;
    return era * 146097 + within * 365 + within / 4 - within / 100 + days;
}
static int64_t calendar_seconds(const struct tm *value)
{
    return civil_days((int64_t)value->tm_year + 1900, (unsigned)value->tm_mon + 1,
        (unsigned)value->tm_mday) * 86400 + value->tm_hour * 3600 + value->tm_min * 60 + value->tm_sec;
}
bool qa_native_process_platform_calendar(void *context, int64_t milliseconds, bool local,
    qa_native_windows_calendar *out, qa_error *error)
{
    if (!out || !qa_native_process_platform_current(context, error)) return false;
    int64_t seconds = milliseconds / 1000 - (milliseconds % 1000 < 0);
    struct tm date, utc;
#if defined(_WIN32)
    __time64_t stamp = seconds;
    if (_gmtime64_s(&utc, &stamp) || (local ? _localtime64_s(&date, &stamp) : _gmtime64_s(&date, &stamp)))
        return fail(error, QA_ERROR_UNSUPPORTED, "Actual platform calendar cannot represent source time");
#else
    time_t stamp = (time_t)seconds;
    if ((int64_t)stamp != seconds || !gmtime_r(&stamp, &utc) ||
        !(local ? localtime_r(&stamp, &date) : gmtime_r(&stamp, &date)))
        return fail(error, QA_ERROR_UNSUPPORTED, "Actual platform calendar cannot represent source time");
#endif
    int64_t timezone = (calendar_seconds(&utc) - calendar_seconds(&date)) / 60;
    if (timezone < INT32_MIN || timezone > INT32_MAX || date.tm_year > INT32_MAX - 1900)
        return fail(error, QA_ERROR_UNSUPPORTED, "Actual platform calendar exceeds native fields");
    int64_t fraction = milliseconds % 1000; if (fraction < 0) fraction += 1000;
    *out = (qa_native_windows_calendar){date.tm_year + 1900, date.tm_mon + 1,
        date.tm_wday, date.tm_mday, date.tm_hour, date.tm_min, date.tm_sec, (int32_t)fraction,
        date.tm_yday, date.tm_isdst, (int32_t)timezone};
    return true;
}

static bool stream_read(void *context, uint64_t offset, void *out, size_t bytes,
    size_t *done, qa_error *error)
{
    (void)offset;
    platform_stream *stream = context;
    if (done) *done = 0;
    if (!stream || !done || (!out && bytes) || stream->closed || !(stream->mode & 1) ||
        !qa_native_process_platform_current(stream->owner, error))
        return fail(error, QA_ERROR_ARGUMENT, "Native standard input lost its actual readable stream");
    ++stream->owner->busy;
#if defined(_WIN32)
    unsigned count = bytes > INT_MAX ? INT_MAX : (unsigned)bytes;
    int result; do { result = _read(stream->descriptor, out, count); } while (result < 0 && errno == EINTR);
#else
    size_t count = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
    ssize_t result; do { result = read(stream->descriptor, out, count); } while (result < 0 && errno == EINTR);
#endif
    --stream->owner->busy;
    if (result < 0) return fail(error, QA_ERROR_IO, "Reading actual native standard input");
    *done = (size_t)result; return true;
}
static bool stream_write(void *context, uint64_t offset, qa_bytes bytes, size_t *done, qa_error *error)
{
    (void)offset;
    platform_stream *stream = context;
    if (done) *done = 0;
    if (!stream || !done || (!bytes.data && bytes.size) || stream->closed || !(stream->mode & 2) ||
        !qa_native_process_platform_current(stream->owner, error))
        return fail(error, QA_ERROR_ARGUMENT, "Native standard output lost its actual writable stream");
    ++stream->owner->busy;
#if defined(_WIN32)
    unsigned count = bytes.size > INT_MAX ? INT_MAX : (unsigned)bytes.size;
    int result; do { result = _write(stream->descriptor, bytes.data, count); } while (result < 0 && errno == EINTR);
#else
    size_t count = bytes.size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes.size;
    ssize_t result; do { result = write(stream->descriptor, bytes.data, count); } while (result < 0 && errno == EINTR);
#endif
    --stream->owner->busy;
    if (result < 0) return fail(error, QA_ERROR_IO, "Writing actual native standard output");
    *done = (size_t)result; return true;
}
static bool windows_stream_read(void *context, void *out, size_t bytes, size_t *done, qa_error *error)
{ return stream_read(context, 0, out, bytes, done, error); }
static bool windows_stream_write(void *context, qa_bytes bytes, qa_error *error)
{
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t done = 0;
        if (!stream_write(context, 0, (qa_bytes){bytes.data + offset, bytes.size - offset}, &done, error)) return false;
        if (!done) return fail(error, QA_ERROR_IO, "Actual native standard output made no progress");
        offset += done;
    }
    return true;
}
static bool stream_size(void *context, uint64_t *out, qa_error *error)
{
    platform_stream *stream = context;
    if (!stream || !out || stream->closed || !qa_native_process_platform_current(stream->owner, error)) return false;
#if defined(_WIN32)
    struct _stat64 info;
    if (_fstat64(stream->descriptor, &info) || info.st_size < 0)
#else
    struct stat info;
    if (fstat(stream->descriptor, &info) || info.st_size < 0)
#endif
        return fail(error, QA_ERROR_IO, "Reading actual native standard stream metadata");
    *out = (uint64_t)info.st_size; return true;
}
static bool stream_flush(void *context, qa_error *error)
{
    platform_stream *stream = context;
    if (!stream || stream->closed || !qa_native_process_platform_current(stream->owner, error)) return false;
    /* Writes bypass stdio buffering. Disk handles still receive a real sync;
     * terminals and pipes have no buffered data in this owner to flush. */
#if defined(_WIN32)
    HANDLE handle = (HANDLE)_get_osfhandle(stream->descriptor);
    if (GetFileType(handle) == FILE_TYPE_DISK && !FlushFileBuffers(handle))
#else
    struct stat info;
    if (fstat(stream->descriptor, &info)) return fail(error, QA_ERROR_IO, "Reading actual native standard stream kind");
    if (S_ISREG(info.st_mode) && fsync(stream->descriptor))
#endif
        return fail(error, QA_ERROR_IO, "Flushing actual native standard stream");
    return true;
}
static bool stream_close(void *context, qa_error *error)
{
    platform_stream *stream = context;
    if (stream && stream->closed) return true;
    if (!stream || !stream->owner || stream->owner->busy)
        return fail(error, QA_ERROR_ARGUMENT, "Native standard stream is inside actual I/O");
    if (stream->held && stream->held->references > 1) {
        --stream->held->references; stream->held = NULL; stream->closed = true; return true;
    }
#if defined(_WIN32)
    int result = _close(stream->descriptor);
#else
    /* Linux close consumes the descriptor even on EINTR; never retry a numeric
     * descriptor that the OS could already have reused for another object. */
    int result = close(stream->descriptor);
#endif
    stream->closed = true; free(stream->held); stream->held = NULL;
    return !result || fail(error, QA_ERROR_IO, "Closing actual native standard descriptor");
}
bool qa_native_process_platform_windows_streams(qa_native_process_platform *owner,
    qa_native_windows_stream out[3], qa_error *error)
{
    if (!out || !qa_native_process_platform_retained(owner, error)) return false;
    for (size_t i = 0; i < 3; ++i) out[i] = (qa_native_windows_stream){owner->streams[i].id,
        i ? NULL : windows_stream_read, i ? windows_stream_write : NULL, owner->streams + i};
    return true;
}
bool qa_native_process_platform_sysv_files(qa_native_process_platform *owner,
    qa_native_sysv_file out[3], bool *terminal, qa_error *error)
{
    if (!out || !terminal || !qa_native_process_platform_retained(owner, error)) return false;
    static const char *const names[3] = {"stdin", "stdout", "stderr"};
    for (size_t i = 0; i < 3; ++i) out[i] = (qa_native_sysv_file){
        .handle = owner->streams[i].id, .capability = owner->streams[i].id,
        .name = names[i], .mode = owner->streams[i].mode,
        .read = i ? NULL : stream_read, .write = i ? stream_write : NULL,
        .size = stream_size, .flush = stream_flush, .close = stream_close,
        .context = owner->streams + i};
    *terminal = owner->terminal;
    return true;
}
static bool platform_fields(qa_source_save_io *io, const qa_native_process_platform *owner)
{
    uint8_t magic[4] = {'Q','N','P','L'};
    uint32_t version = 2; uint64_t id = owner->id; int64_t frequency = owner->frequency;
    bool terminal = owner->terminal;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNPL", 4) ||
        !qa_source_save_u32(io, &version) || version != 2 ||
        !qa_source_save_u64(io, &id) || id != owner->id ||
        !qa_source_save_i64(io, &frequency) || frequency != owner->frequency ||
        !qa_source_save_bool(io, &terminal) || terminal != owner->terminal) return false;
    for (size_t i = 0; i < 3; ++i) {
        const platform_stream *row = owner->streams + i;
        uint64_t saved_id = row->id, device = row->device, object = row->durable_object ? row->object : 0;
        bool durable_object = row->durable_object;
        uint32_t mode = row->mode; bool closed = row->closed;
        if (!qa_source_save_u64(io, &saved_id) || saved_id != row->id ||
            !qa_source_save_u64(io, &device) || device != row->device ||
            !qa_source_save_bool(io, &durable_object) || durable_object != row->durable_object ||
            !qa_source_save_u64(io, &object) || object != (row->durable_object ? row->object : 0) ||
            !qa_source_save_u32(io, &mode) || mode != row->mode ||
            !qa_source_save_bool(io, &closed) || closed != row->closed) return false;
    }
    return true;
}
bool qa_native_process_platform_checkpoint(const qa_native_process_platform *owner, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !owner || owner->busy ||
        !qa_native_process_platform_retained(owner, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && platform_fields(&io, owner) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool qa_native_process_platform_validate(const qa_native_process_platform *owner, qa_bytes bytes, qa_error *error)
{
    if (!qa_native_process_platform_retained(owner, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && platform_fields(&io, owner) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    return okay || fail(error, QA_ERROR_FORMAT, "Native platform continuation differs from its retained resources");
}
void qa_native_process_platform_retain(qa_native_process_platform *owner)
{ if (owner) ++owner->references; }
bool qa_native_process_platform_release(qa_native_process_platform **pointer, qa_error *error)
{
    if (!pointer) return fail(error, QA_ERROR_ARGUMENT, "Native platform release requires its actual owner");
    qa_native_process_platform *owner = *pointer;
    if (!owner) return true;
    if (owner->busy) return fail(error, QA_ERROR_ARGUMENT, "Native platform is inside actual I/O");
    if (owner->references > 1) { --owner->references; *pointer = NULL; return true; }
    owner->closing = true;
    bool okay = true; qa_error first = {0};
    for (size_t i = 0; i < 3; ++i) {
        qa_error failure = {0};
        if (!stream_close(owner->streams + i, &failure) && okay) { okay = false; first = failure; }
    }
    if (!okay) { if (error) *error = first; return false; }
    free(owner); *pointer = NULL; return true;
}
