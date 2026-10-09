#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include "qa/platform_services.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

static bool clock_error(qa_error *error)
{
#if defined(_WIN32)
    qa_error_set(error, QA_ERROR_IO, (size_t)GetLastError(), "Reading platform clock");
#else
    int code = errno;
    qa_error_set(error, QA_ERROR_IO, (size_t)code, "Reading platform clock: %s", strerror(code));
#endif
    return false;
}

bool qa_platform_performance_read(int64_t *counter, int64_t *frequency, qa_error *error)
{
#if defined(_WIN32)
    LARGE_INTEGER ticks, rate;
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&rate)) return clock_error(error);
    *counter = ticks.QuadPart;
    *frequency = rate.QuadPart;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return clock_error(error);
    *counter = (int64_t)value.tv_sec * INT64_C(1000000000) + value.tv_nsec;
    *frequency = INT64_C(1000000000);
#endif
    return true;
}

int64_t qa_platform_clock_ticks_per_second(void)
{
#if defined(_WIN32)
    return -1;
#else
    return (int64_t)sysconf(_SC_CLK_TCK);
#endif
}

bool qa_platform_clock_read(qa_platform_clock_kind kind, qa_platform_timespec *out, qa_error *error)
{
#if defined(_WIN32)
    uint64_t ticks;
    if (kind == QA_PLATFORM_CLOCK_MONOTONIC) {
        int64_t counter, frequency;
        if (!qa_platform_performance_read(&counter, &frequency, error)) return false;
        *out = (qa_platform_timespec){counter / frequency,
            (int32_t)((long double)(counter % frequency) * 1000000000.0L / (long double)frequency)};
        return true;
    }
    if (kind == QA_PLATFORM_CLOCK_REALTIME) {
        FILETIME value;
        GetSystemTimeAsFileTime(&value);
        ticks = (uint64_t)value.dwHighDateTime << 32 | value.dwLowDateTime;
        const uint64_t epoch = UINT64_C(116444736000000000);
        if (ticks >= epoch) {
            ticks -= epoch;
            *out = (qa_platform_timespec){(int64_t)(ticks / UINT64_C(10000000)),
                (int32_t)(ticks % UINT64_C(10000000)) * 100};
        } else {
            uint64_t before = epoch - ticks;
            uint64_t remainder = before % UINT64_C(10000000);
            *out = (qa_platform_timespec){-(int64_t)(before / UINT64_C(10000000)) - (remainder != 0),
                remainder ? (int32_t)(UINT64_C(10000000) - remainder) * 100 : 0};
        }
        return true;
    }
    if (kind == QA_PLATFORM_CLOCK_PROCESS_CPU || kind == QA_PLATFORM_CLOCK_THREAD_CPU) {
        FILETIME created, exited, kernel, user;
        BOOL okay = kind == QA_PLATFORM_CLOCK_PROCESS_CPU ?
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) :
            GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
        if (!okay) return clock_error(error);
        ticks = ((uint64_t)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) +
            ((uint64_t)user.dwHighDateTime << 32 | user.dwLowDateTime);
        *out = (qa_platform_timespec){(int64_t)(ticks / UINT64_C(10000000)),
            (int32_t)(ticks % UINT64_C(10000000)) * 100};
        return true;
    }
#else
    clockid_t native;
    switch (kind) {
    case QA_PLATFORM_CLOCK_REALTIME: native = CLOCK_REALTIME; break;
    case QA_PLATFORM_CLOCK_MONOTONIC: native = CLOCK_MONOTONIC; break;
#if defined(CLOCK_PROCESS_CPUTIME_ID)
    case QA_PLATFORM_CLOCK_PROCESS_CPU: native = CLOCK_PROCESS_CPUTIME_ID; break;
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
    case QA_PLATFORM_CLOCK_THREAD_CPU: native = CLOCK_THREAD_CPUTIME_ID; break;
#endif
#if defined(CLOCK_MONOTONIC_RAW)
    case QA_PLATFORM_CLOCK_MONOTONIC_RAW: native = CLOCK_MONOTONIC_RAW; break;
#endif
#if defined(CLOCK_REALTIME_COARSE)
    case QA_PLATFORM_CLOCK_REALTIME_COARSE: native = CLOCK_REALTIME_COARSE; break;
#endif
#if defined(CLOCK_MONOTONIC_COARSE)
    case QA_PLATFORM_CLOCK_MONOTONIC_COARSE: native = CLOCK_MONOTONIC_COARSE; break;
#endif
#if defined(CLOCK_BOOTTIME)
    case QA_PLATFORM_CLOCK_BOOTTIME: native = CLOCK_BOOTTIME; break;
#endif
#if defined(CLOCK_REALTIME_ALARM)
    case QA_PLATFORM_CLOCK_REALTIME_ALARM: native = CLOCK_REALTIME_ALARM; break;
#endif
#if defined(CLOCK_BOOTTIME_ALARM)
    case QA_PLATFORM_CLOCK_BOOTTIME_ALARM: native = CLOCK_BOOTTIME_ALARM; break;
#endif
#if defined(CLOCK_TAI)
    case QA_PLATFORM_CLOCK_TAI: native = CLOCK_TAI; break;
#endif
    default: goto unsupported;
    }
    struct timespec value;
    if (clock_gettime(native, &value)) return clock_error(error);
    *out = (qa_platform_timespec){(int64_t)value.tv_sec, (int32_t)value.tv_nsec};
    return true;
unsupported:
#endif
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Clock kind is unavailable on this platform");
    return false;
}

uint64_t qa_platform_time_ns(void)
{
    qa_platform_timespec value;
    if (!qa_platform_clock_read(QA_PLATFORM_CLOCK_MONOTONIC, &value, NULL)) return 0;
    return (uint64_t)value.seconds * UINT64_C(1000000000) + (uint32_t)value.nanoseconds;
}

void qa_platform_sleep_ns(uint64_t duration)
{
    uint64_t milliseconds = duration / UINT64_C(1000000);
#if defined(_WIN32)
    while (milliseconds >= UINT32_MAX) { Sleep(UINT32_MAX - 1); milliseconds -= UINT32_MAX - 1; }
    if (milliseconds) Sleep((DWORD)milliseconds);
#else
    while (milliseconds) {
        uint64_t part = milliseconds > UINT32_MAX ? UINT32_MAX : milliseconds;
        struct timespec delay = {(time_t)(part / 1000), (long)(part % 1000) * 1000000L};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        milliseconds -= part;
    }
#endif
}

double qa_platform_utc_ms(void)
{
    qa_platform_timespec value;
    if (!qa_platform_clock_read(QA_PLATFORM_CLOCK_REALTIME, &value, NULL)) return NAN;
    return (double)value.seconds * 1000 + (double)value.nanoseconds / 1000000;
}

double qa_platform_tick_ms(void)
{
    return 0.000001;
}

bool qa_platform_random(void *out, size_t bytes, uint32_t flags,
    size_t *completed, int32_t *native_errno, qa_error *error)
{
    *completed = 0; *native_errno = 0;
#if defined(__linux__)
    (void)error;
    ssize_t received = syscall(SYS_getrandom, out, bytes, flags);
    if (received < 0) *native_errno = errno;
    else *completed = (size_t)received;
    return true;
#else
    if (flags) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, flags,
            "Linux random flags are unavailable on this platform");
        return false;
    }
    if (!qa_platform_entropy(out, bytes, error)) return false;
    *completed = bytes;
    return true;
#endif
}

bool qa_platform_entropy(void *out, size_t bytes, qa_error *error)
{
#if defined(_WIN32)
    size_t offset = 0;
    while (offset < bytes) {
        ULONG count = bytes - offset > ULONG_MAX ? ULONG_MAX : (ULONG)(bytes - offset);
        if (BCryptGenRandom(NULL, (PUCHAR)out + offset, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            qa_error_set(error, QA_ERROR_IO, 0, "Reading platform entropy");
            return false;
        }
        offset += count;
    }
#elif defined(__APPLE__)
    if (bytes) arc4random_buf(out, bytes);
#elif defined(__linux__)
    size_t offset = 0;
    while (offset < bytes) {
        size_t count = bytes - offset > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes - offset;
        size_t received;
        int32_t code;
        if (!qa_platform_random((uint8_t *)out + offset, count, 0, &received, &code, error))
            return false;
        if (code == EINTR) continue;
        if (code || !received) {
            if (!code) code = EIO;
            qa_error_set(error, QA_ERROR_IO, (size_t)code,
                "Reading platform entropy: %s", strerror(code));
            return false;
        }
        offset += received;
    }
#else
    size_t offset = 0;
    int descriptor = bytes ? open("/dev/urandom", O_RDONLY | O_CLOEXEC) : -1;
    if (bytes && descriptor < 0) {
        qa_error_set(error, QA_ERROR_IO, (size_t)errno, "Opening platform entropy");
        return false;
    }
    while (offset < bytes) {
        size_t count = bytes - offset > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes - offset;
        ssize_t received = read(descriptor, (uint8_t *)out + offset, count);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) {
            int code = received < 0 ? errno : EIO;
            close(descriptor);
            qa_error_set(error, QA_ERROR_IO, (size_t)code, "Reading platform entropy: %s", strerror(code));
            return false;
        }
        offset += (size_t)received;
    }
    if (descriptor >= 0) close(descriptor);
#endif
    return true;
}

bool qa_platform_entropy_u64(uint64_t *out, qa_error *error)
{
    return qa_platform_entropy(out, sizeof(*out), error);
}

static bool calendar_rows(int64_t milliseconds, bool local, struct tm *date, struct tm *utc)
{
    int64_t seconds = milliseconds / 1000 - (milliseconds % 1000 < 0);
#if defined(_WIN32)
    __time64_t stamp = seconds;
    return !_gmtime64_s(utc, &stamp) &&
        !(local ? _localtime64_s(date, &stamp) : _gmtime64_s(date, &stamp));
#else
    time_t stamp = (time_t)seconds;
    return (int64_t)stamp == seconds && gmtime_r(&stamp, utc) &&
        (local ? localtime_r(&stamp, date) : gmtime_r(&stamp, date));
#endif
}

static int64_t calendar_seconds(const struct tm *value)
{
    int64_t year = (int64_t)value->tm_year + 1900;
    unsigned month = (unsigned)value->tm_mon + 1;
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned within = (unsigned)(year - era * 400);
    unsigned shifted = month > 2 ? month - 3 : month + 9;
    unsigned days = (153 * shifted + 2) / 5 + (unsigned)value->tm_mday - 1;
    return (era * 146097 + within * 365 + within / 4 - within / 100 + days) * 86400 +
        value->tm_hour * 3600 + value->tm_min * 60 + value->tm_sec;
}

bool qa_platform_calendar(int64_t milliseconds, bool local, qa_platform_calendar_fields *out, qa_error *error)
{
    struct tm date, utc;
    if (!calendar_rows(milliseconds, local, &date, &utc)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Platform calendar cannot represent timestamp");
        return false;
    }
    int64_t timezone_seconds = calendar_seconds(&utc) - calendar_seconds(&date);
    int64_t timezone_minutes = timezone_seconds / 60;
    if (date.tm_year > INT32_MAX - 1900 || timezone_minutes < INT32_MIN || timezone_minutes > INT32_MAX) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Platform calendar exceeds native fields");
        return false;
    }
    int64_t fraction = milliseconds % 1000;
    if (fraction < 0) fraction += 1000;
    *out = (qa_platform_calendar_fields){
        .unix_seconds = milliseconds / 1000 - (milliseconds % 1000 < 0),
        .year = date.tm_year + 1900, .month = date.tm_mon + 1, .day = date.tm_mday,
        .weekday = date.tm_wday, .yearday = date.tm_yday,
        .hour = date.tm_hour, .minute = date.tm_min, .second = date.tm_sec,
        .millisecond = (int32_t)fraction, .daylight = date.tm_isdst,
        .timezone_minutes = (int32_t)timezone_minutes,
        .timezone_seconds = timezone_seconds};
#if defined(__linux__) || defined(__APPLE__)
    const char *zone = date.tm_zone;
    size_t length = zone ? strnlen(zone, sizeof(out->timezone) - 1) : 0;
    if (length) memcpy(out->timezone, zone, length);
#else
    char zone[256];
    size_t length = strftime(zone, sizeof(zone), "%Z", &date);
    if (length >= sizeof(out->timezone)) length = sizeof(out->timezone) - 1;
    if (length) memcpy(out->timezone, zone, length);
#endif
    return true;
}

bool qa_platform_calendar_format(int64_t milliseconds, const char *format, char *text, size_t capacity)
{
    struct tm date, utc;
    return calendar_rows(milliseconds, true, &date, &utc) && strftime(text, capacity, format, &date) != 0;
}
