#ifndef QA_PLATFORM_SERVICES_H
#define QA_PLATFORM_SERVICES_H

#include "qa/common.h"

typedef enum qa_platform_clock_kind {
    QA_PLATFORM_CLOCK_REALTIME,
    QA_PLATFORM_CLOCK_MONOTONIC,
    QA_PLATFORM_CLOCK_PROCESS_CPU,
    QA_PLATFORM_CLOCK_THREAD_CPU,
    QA_PLATFORM_CLOCK_MONOTONIC_RAW,
    QA_PLATFORM_CLOCK_REALTIME_COARSE,
    QA_PLATFORM_CLOCK_MONOTONIC_COARSE,
    QA_PLATFORM_CLOCK_BOOTTIME,
    QA_PLATFORM_CLOCK_REALTIME_ALARM,
    QA_PLATFORM_CLOCK_BOOTTIME_ALARM,
    QA_PLATFORM_CLOCK_TAI
} qa_platform_clock_kind;

typedef struct qa_platform_timespec {
    int64_t seconds;
    int32_t nanoseconds;
} qa_platform_timespec;

typedef struct qa_platform_calendar_fields {
    int64_t unix_seconds;
    int32_t year, month, day, weekday, yearday;
    int32_t hour, minute, second, millisecond, daylight;
    /* UTC minus local time, matching the native Windows timezone convention. */
    int32_t timezone_minutes;
    int64_t timezone_seconds; /* UTC minus local time; exact native offset. */
    char timezone[64]; /* Bounded native abbreviation from this conversion. */
} qa_platform_calendar_fields;

bool qa_platform_clock_read(qa_platform_clock_kind, qa_platform_timespec *, qa_error *);
int64_t qa_platform_clock_ticks_per_second(void);
/* Native QPC ticks/frequency on Windows; monotonic nanoseconds/1e9 on POSIX. */
bool qa_platform_performance_read(int64_t *counter, int64_t *frequency, qa_error *);
uint64_t qa_platform_time_ns(void);
void qa_platform_sleep_ns(uint64_t);
double qa_platform_utc_ms(void);
/* Renderer instrumentation stores nanoseconds. */
double qa_platform_tick_ms(void);

/* One native operation. A completed syscall returns true, including errno:
 * completed is the actual prefix and native_errno is zero on success. */
bool qa_platform_random(void *, size_t, uint32_t flags, size_t *completed,
                        int32_t *native_errno, qa_error *);
bool qa_platform_entropy(void *, size_t, qa_error *);
bool qa_platform_entropy_u64(uint64_t *, qa_error *);
bool qa_platform_calendar(int64_t unix_ms, bool local, qa_platform_calendar_fields *, qa_error *);
/* Locale formatting of an explicit timestamp, used for save-list dates. */
bool qa_platform_calendar_format(int64_t unix_ms, const char *format,
                                 char *text, size_t capacity);

#endif
